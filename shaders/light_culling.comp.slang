import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;

struct LightCullHeapPC {
    uint globalUbo;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
};

[[vk::push_constant]] LightCullHeapPC pc;

static const uint kMaxLightIndexList = 65536u;

struct LightIndexHead {
    uint globalIndexCount;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    StructuredBuffer<LightUBOBlock> lightBlock = DescriptorHandle<StructuredBuffer<LightUBOBlock>>(pc.lightBuffer);
    RWStructuredBuffer<LightGrid> lightGrid = DescriptorHandle<RWStructuredBuffer<LightGrid>>(pc.lightGrid);
    RWStructuredBuffer<uint> indexList = DescriptorHandle<RWStructuredBuffer<uint>>(pc.lightIndexList);

    uint cx = id.x;
    uint cy = id.y;
    uint cz = id.z;

    if (cx >= ubo.gridDimX || cy >= ubo.gridDimY || cz >= ubo.gridDimZ) return;

    // Логарифмическое разделение глубины
    float zTileNear = ubo.zNear * pow(ubo.zFar / ubo.zNear, float(cz) / float(ubo.gridDimZ));
    float zTileFar  = ubo.zNear * pow(ubo.zFar / ubo.zNear, float(cz + 1) / float(ubo.gridDimZ));

    uint lightCount = 0;
    uint localIndices[100];

    int activeLights = max(lightBlock[0].activeLightsCount, 0);
    for (int i = 0; i < activeLights && i < 100; i++) {
        PointLightData light = lightBlock[0].lights[i];
        int type = int(light.posType.w);
        
        if (type == 0 || type == 4) { // Directional или Ambient видны всегда
            localIndices[lightCount++] = i;
            continue;
        }

        float3 viewPos = mul(ubo.view, float4(light.posType.xyz, 1.0)).xyz;
        float radius = light.dirRadius.w;

        // Грубая проверка по глубине (View Space Z смотрит в минус)
        if (-viewPos.z + radius < zTileNear || -viewPos.z - radius > zTileFar) continue;

        // Точная проверка по осям X и Y (Screen-Space AABB пересечение с тайлом)
        float4 centerClip = mul(ubo.projection, float4(viewPos, 1.0));
        float3 centerNdc = centerClip.xyz / centerClip.w;
        float rClip = abs(ubo.projection[1][1] * radius / viewPos.z);
        
        float2 lightMinUV = (centerNdc.xy - rClip) * 0.5 + 0.5;
        float2 lightMaxUV = (centerNdc.xy + rClip) * 0.5 + 0.5;
        float2 tileMinUV = float2(cx, cy) / float2(ubo.gridDimX, ubo.gridDimY);
        float2 tileMaxUV = float2(cx + 1, cy + 1) / float2(ubo.gridDimX, ubo.gridDimY);

        if (lightMaxUV.x < tileMinUV.x || lightMinUV.x > tileMaxUV.x ||
            lightMaxUV.y < tileMinUV.y || lightMinUV.y > tileMaxUV.y) continue;

        localIndices[lightCount++] = i;
    }

    uint offset = 0;
    InterlockedAdd(indexList[0], lightCount, offset);
    if (offset >= kMaxLightIndexList - 1u) {
        lightCount = 0u;
    } else if (offset + lightCount > kMaxLightIndexList - 1u) {
        lightCount = kMaxLightIndexList - 1u - offset;
    }
    for (uint i = 0; i < lightCount; i++) {
        indexList[offset + 1u + i] = localIndices[i];
    }

    uint clusterIdx = cx + cy * ubo.gridDimX + cz * ubo.gridDimX * ubo.gridDimY;
    lightGrid[clusterIdx].offset = offset;
    lightGrid[clusterIdx].count = lightCount;
}
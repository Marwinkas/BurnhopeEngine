import common.SlangBuiltins;
import common.Bindless;
import common.GlobalUbo;
import common.SceneGpu;
import common.RayTrace;

struct ProbeRenderHeapPC {
    uint globalUbo;
    uint giCascade0;
    uint outProbeTex;
    uint rtTlas;
    uint objectStorage;
    uint materialStorage;
    uint defaultSampler;
    float4 probePos;
    int resolution;
};

[[vk::push_constant]] ProbeRenderHeapPC pc;

struct MaterialData {
    int albedoAlphaIdx;
    int normalIdx;
    int ormxIdx;
    int emissiveIdx;
    int useTriplanar;
    int isTransparent;
    int repeatTexture;
    int pad1;
    float2 uvScale;
    float triplanarScale;
    float emissiveIntensity;
    float4 albedoColor;
    float4 emissiveColor;
    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;
    float aoStrength;
    float pad2;
    float pad3;
    float pad4;
};

float3 CalculateSky(GlobalUbo ubo, float3 viewDir) {
    float t = max(viewDir.y, 0.0);
    float3 skyColor = lerp(ubo.skyHorizonColor.rgb, ubo.skyZenithColor.rgb, pow(t, 0.5));
    if (ubo.fogParams.x > 0.5) {
        skyColor = lerp(ubo.fogColor.rgb, skyColor, pow(t, 0.5));
    }
    float sunDot = dot(viewDir, -ubo.sunDir);
    float sunSize = ubo.skySunParams.x;
    float sunGlow = ubo.skySunParams.y;
    float sunGlowSize = max(ubo.skySunParams.z, 0.001);
    float sunDisk = smoothstep(1.0 - sunSize - 0.001, 1.0 - sunSize, sunDot);
    float sunAura = pow(max(sunDot, 0.0), 1.0 / sunGlowSize) * sunGlow;
    float3 sunRenderColor = ubo.sunColor * ubo.sunIntensity;
    skyColor += sunRenderColor * sunDisk;
    skyColor += sunRenderColor * sunAura * 0.1;
    return skyColor;
}

float3 octaDecode(float2 uv) {
    uv = uv * 2.0 - 1.0;
    float3 v = float3(uv.x, uv.y, 1.0 - abs(uv.x) - abs(uv.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}

static const float PI = 3.14159265359;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    RaytracingAccelerationStructure tlas = heapTlas(pc.rtTlas);
    RWTexture2D<float4> outProbeTex = heapRWTex2D(pc.outProbeTex);
    StructuredBuffer<ObjectData> objBuf = DescriptorHandle<StructuredBuffer<ObjectData>>(pc.objectStorage);
    StructuredBuffer<MaterialData> matBuf = DescriptorHandle<StructuredBuffer<MaterialData>>(pc.materialStorage);

    int2 pixel = int2(id.xy);
    if (pixel.x >= pc.resolution || pixel.y >= pc.resolution) return;

    float2 uv = (float2(pixel) + 0.5) / float(pc.resolution);
    float3 dir = octaDecode(uv);

    float3 color = float3(0.0);
    RtHitInfo hit;
    if (rtTraceOpaque(tlas, pc.probePos.xyz, dir, 1000.0, hit)) {
        float u = hit.barycentrics.x;
        float v = hit.barycentrics.y;
        float w = 1.0 - u - v;

        uint objIdx = hit.instanceCustomIndex + hit.geometryIndex;
        if (objIdx < 65536u) {
        ObjectData obj = objBuf[objIdx];
        uint matIdx = min(obj.materialID, 65535u);
        MaterialData mat = matBuf[matIdx];
        float3 albedo = float3(0.8);

        StructuredBuffer<uint> indices = heapStructured<uint>(obj.indexHeap);
        StructuredBuffer<PackedVertexAttr> attributes = heapStructured<PackedVertexAttr>(obj.attrHeap);
        uint triBase = hit.primitiveIndex * 3u;
        PackedVertexAttr v0_raw = attributes[indices[triBase + 0]];
        PackedVertexAttr v1_raw = attributes[indices[triBase + 1]];
        PackedVertexAttr v2_raw = attributes[indices[triBase + 2]];

        float2 texUV = unpackUV(v0_raw.texUV) * w + unpackUV(v1_raw.texUV) * u + unpackUV(v2_raw.texUV) * v;
        float3 localNormal = getNormalFromQTangent(v0_raw.qTangent) * w
            + getNormalFromQTangent(v1_raw.qTangent) * u
            + getNormalFromQTangent(v2_raw.qTangent) * v;
        float3x3 normalMatrix = inverse3x3(float3x3(obj.modelMatrix));
        float3 worldNormal = normalize(mul(normalMatrix, localNormal));
        if (dot(worldNormal, dir) > 0.0) worldNormal = -worldNormal;

        if (mat.albedoAlphaIdx >= 0) {
            albedo = heapTex2D(uint(mat.albedoAlphaIdx))
                         .SampleLevel(sam, texUV * mat.uvScale, 0.0)
                         .rgb
                     * mat.albedoColor.rgb;
        } else {
            albedo = mat.albedoColor.rgb;
        }

        float3 L = normalize(-ubo.sunDir);
        float NdotL = max(dot(worldNormal, L), 0.0);
        float shadow = 1.0;
        if (NdotL > 0.0) {
            float3 hitPos = pc.probePos.xyz + dir * hit.t;
            if (rtTraceShadow(tlas, hitPos + worldNormal * 0.02 + L * 0.02, L, 1000.0)) {
                shadow = 0.0;
            }
        }

        float3 skyAmbient = lerp(ubo.skyHorizonColor.rgb, ubo.skyZenithColor.rgb, max(0.0, worldNormal.y))
            * ubo.sunIntensity * 0.2;
        color = (albedo / PI) * ubo.sunColor * ubo.sunIntensity * (NdotL * shadow) + albedo * skyAmbient;
        }
    } else {
        color = CalculateSky(ubo, dir);
    }

    outProbeTex[pixel] = float4(color, 1.0);
}

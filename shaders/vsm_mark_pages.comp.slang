import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;

struct VsmMarkHeapPC {
    uint globalUbo;
    uint gbufferDepth;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint vsmAllocator;
};

[[vk::push_constant]] VsmMarkHeapPC pc;


// Используем размер группы 16x16 для прохода по пикселям экрана




static const uint VIRTUAL_PAGES_X = 4096;
static const uint PAGE_SIZE = 128;
static const uint kMaxLights = 100u;
static const uint kMaxLightIndexList = 65536u;

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(0u);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    RWStructuredBuffer<LightGrid> lightGridBuf = DescriptorHandle<RWStructuredBuffer<LightGrid>>(pc.lightGrid);
    StructuredBuffer<uint> globalIndexList = DescriptorHandle<StructuredBuffer<uint>>(pc.lightIndexList);
    StructuredBuffer<PointFaceMatrices> faceMatricesBuf = DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices);
    RWStructuredBuffer<uint> virtualPages = DescriptorHandle<RWStructuredBuffer<uint>>(pc.vsmAllocator);
    StructuredBuffer<LightUBOBlock> lightBlock = DescriptorHandle<StructuredBuffer<LightUBOBlock>>(pc.lightBuffer);

    int2 uv = int2(id.xy);
    float2 texSize = ubo.screenSize.xy;
    
    if (uv.x >= int(texSize.x) || uv.y >= int(texSize.y)) return;

    float depth = gDepth.Load(int3(uv, 0)).x;
    if (depth >= 1.0) return; // Пропускаем небо

    // 1. Восстанавливаем позицию в мире
    float2 ndc = (float2(uv) / texSize) * 2.0 - 1.0;
    float4 clip = float4(ndc, depth, 1.0);
    float4 worldPosW = mul(ubo.invViewProj, clip); float3 worldPos = worldPosW.xyz / worldPosW.w;

    // 2. Определяем кластер для поиска источников света
    float4 viewPos = mul(ubo.view, float4(worldPos, 1.0));
    float zView = max(-viewPos.z, ubo.zNear + 0.001); 
    
    uint clusterX = uint(float(uv.x) / (texSize.x / float(ubo.gridDimX)));
    uint clusterY = uint(float(uv.y) / (texSize.y / float(ubo.gridDimY)));
    uint clusterZ = uint(max(0.0, log(zView / ubo.zNear) * float(ubo.gridDimZ) / log(ubo.zFar / ubo.zNear)));
    
    clusterX = clamp(clusterX, 0u, ubo.gridDimX - 1u);
    clusterY = clamp(clusterY, 0u, ubo.gridDimY - 1u);
    clusterZ = clamp(clusterZ, 0u, ubo.gridDimZ - 1u);
    
    uint totalClusters = ubo.gridDimX * ubo.gridDimY * ubo.gridDimZ;
    uint clusterIdx = clusterX + clusterY * ubo.gridDimX + clusterZ * ubo.gridDimX * ubo.gridDimY;
    clusterIdx = clamp(clusterIdx, 0u, totalClusters - 1u);

    uint offset = lightGridBuf[clusterIdx].offset;
    uint lightsInCluster = min(lightGridBuf[clusterIdx].count, kMaxLights);
    int activeLights = max(lightBlock[0].activeLightsCount, 0);
    activeLights = min(activeLights, int(kMaxLights));

    // 3. Проходимся по всем источникам в кластере
    for (uint i = 0; i < lightsInCluster; ++i) {
        uint listIdx = offset + 1u + i;
        if (listIdx >= kMaxLightIndexList) break;
        uint lightIdx = globalIndexList[listIdx];
        lightIdx = min(lightIdx, uint(max(activeLights - 1, 0)));
        PointLightData light = lightBlock[0].lights[lightIdx];
        
        if (int(light.shadowParams.z + 0.5) != 1) continue; // Если теней нет

        int type = int(light.posType.w);
        int encodedSlot = int(light.shadowParams.w);
        if (encodedSlot < 0) continue;

        int realSlot = encodedSlot / 10000;
        int tileSize = encodedSlot % 10000;
        float pagesPerFace = float(tileSize) / float(PAGE_SIZE);

        uint faceCount = (type == 1) ? 6 : 1;

        for (uint face = 0; face < faceCount; ++face) {
            float4x4 lightMat = (type == 2) ? light.lightSpaceMatrix : faceMatricesBuf[lightIdx].faces[face];
            float4 lightClip = mul(lightMat, float4(worldPos, 1.0));
            float3 proj = lightClip.xyz / lightClip.w;
            proj.xy = proj.xy * 0.5 + 0.5;

            // Проверяем, попадает ли точка в область видимости этой грани
            if (proj.z >= 0.0 && proj.z <= 1.0 && proj.x >= 0.0 && proj.x <= 1.0 && proj.y >= 0.0 && proj.y <= 1.0) {
                
                // Вычисляем координаты в виртуальной таблице страниц
                uint slotIndex = lightIdx * 6 + face;
                uint vOffsetX = (slotIndex % 32) * PAGE_SIZE;
                uint vOffsetY = (slotIndex / 32) * PAGE_SIZE;

                uint vPageX = vOffsetX + uint(proj.x * pagesPerFace);
                uint vPageY = vOffsetY + uint(proj.y * pagesPerFace);
                uint vPageIdx = vPageY * VIRTUAL_PAGES_X + vPageX;

                // Вычисляем индекс физической страницы в атласе
                uint baseX = uint(realSlot % 32) * PAGE_SIZE;
                uint baseY = uint(realSlot / 32) * PAGE_SIZE;
                
                // Логика смещения граней внутри слота
                uint faceX = baseX + int(face) * tileSize;
                uint faceY = baseY;
                while (faceX + tileSize > 4096) {
                    faceX -= 4096;
                    faceY += tileSize;
                }

                uint physPageX = (faceX / PAGE_SIZE) + uint(proj.x * pagesPerFace);
                uint physPageY = (faceY / PAGE_SIZE) + uint(proj.y * pagesPerFace);
                uint physIdx = physPageY * 32 + physPageX;

                // Записываем соответствие
                virtualPages[vPageIdx] = physIdx;
            }
        }
    }
}
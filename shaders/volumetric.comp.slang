import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;
import common.VsmSample;

struct VolumetricHeapPC {
    uint globalUbo;
    uint gbufferDepth;
    uint gbufferPortalId;
    uint shadowCsm;
    uint shadowAtlas;
    uint blueNoise;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint vsmAtlas;
    uint vsmPageTable;
    uint volumetricOut;
    uint portalUbos;
    uint defaultSampler;
};

[[vk::push_constant]] VolumetricHeapPC pc;

static const uint kMaxLights = 100u;
static const uint kMaxLightIndexList = 65536u;






// Быстрый поиск теней для волюметрики (1 tap hard shadows)
float SunShadowVolumetric(float3 pos, GlobalUbo currentUbo) {
    float depth = abs((mul(currentUbo.view, float4(pos, 1.0))).z);
    int layer = 3;
    for(int i = 0; i < 4; i++) {
        if(depth < currentUbo.cascadeSplits[i] + 0.5) { layer = i; break; }
    }
    float4 fragPosLightSpace = mul(currentUbo.sunLightSpaceMatrices[layer], float4(pos, 1.0));
    float3 proj = fragPosLightSpace.xyz / fragPosLightSpace.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;
    float shadowDepth = heapTex2DArray(pc.shadowCsm).SampleLevel(heapSampler(pc.defaultSampler), float3(proj.xy, float(layer)), 0.0).r;
    return (proj.z > shadowDepth + 0.001) ? 0.0 : 1.0;
}

float SampleVirtualShadowMapVolumetric(float3 projCoords, uint lightIndex, uint face, int encodedSlot) {
    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;
    float pagesPerFace = float(encodedSlot % 10000) / 128.0;
    uint slotIndex = lightIndex * 6 + face;
    uint offsetX = (slotIndex % 32) * 128;
    uint offsetY = (slotIndex / 32) * 128;

    uint localX = clamp(uint(projCoords.x * pagesPerFace), 0u, uint(pagesPerFace - 1.0));
    uint localY = clamp(uint(projCoords.y * pagesPerFace), 0u, uint(pagesPerFace - 1.0));
    uint physPage = (*DescriptorHandle<StructuredBuffer<uint>>(pc.vsmPageTable))[(offsetY + localY) * 4096 + (offsetX + localX)];

    if (physPage != 0xFFFFFFFF && physPage != 0xFFFFFFFE) {
        uint pX = physPage % 32;
        uint pY = physPage / 32;
        float2 pUV = frac(projCoords.xy * pagesPerFace);
        float2 physTexelCoord = float2(pX * 128.0, pY * 128.0) + pUV * 128.0;
        float shadowDepth = heapTex2D_r(pc.vsmAtlas).SampleLevel(heapSampler(pc.defaultSampler), physTexelCoord / 4096.0, 0.0).r;
        return (projCoords.z - 0.005 > shadowDepth) ? 0.0 : 1.0;
    }
    return 1.0;
}

float PointShadowVolumetric(float3 pos, PointLightData light, uint lightIdx) {
    if (light.shadowParams.w < 0.0) return 1.0;
    float3 fragToLight = pos - light.posType.xyz;
    float3 absVec = abs(fragToLight);
    int face = 0;
    if (absVec.x >= absVec.y && absVec.x >= absVec.z) face = fragToLight.x > 0.0 ? 0 : 1;
    else if (absVec.y >= absVec.x && absVec.y >= absVec.z) face = fragToLight.y > 0.0 ? 2 : 3;
    else face = fragToLight.z > 0.0 ? 4 : 5;

    float4x4 faceMat = (*DescriptorHandle<StructuredBuffer<PointFaceMatrices>>(pc.faceMatrices))[lightIdx].faces[face];
    float4 fragLightSpace = mul(faceMat, float4(pos, 1.0));
    float3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj.xy = proj.xy * 0.5 + 0.5; proj.y = 1.0 - proj.y;
    return SampleVirtualShadowMapVolumetric(proj, lightIdx, uint(face), int(light.shadowParams.w));
}

float SpotShadowVolumetric(float3 pos, PointLightData light, uint lightIdx) {
    if (light.shadowParams.w < 0.0) return 1.0;
    float4 fragLightSpace = mul(light.lightSpaceMatrix, float4(pos, 1.0));
    float3 proj = fragLightSpace.xyz / fragLightSpace.w;
    proj.xy = proj.xy * 0.5 + 0.5; proj.y = 1.0 - proj.y;
    return SampleVirtualShadowMapVolumetric(proj, lightIdx, 0, int(light.shadowParams.w));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 pixelCoords = int2(id.xy);
    int2 texSize = int2(loadGlobalUbo(pc.globalUbo).screenSize.xy * 0.5);
    if (pixelCoords.x >= texSize.x || pixelCoords.y >= texSize.y) return;

    int2 fullResCoords = pixelCoords * 2;
    uint portal_id = heapTex2D_u(pc.gbufferPortalId).Load(int3(fullResCoords, 0)).x;
    GlobalUbo currentUbo = loadGlobalUbo(pc.globalUbo);
    if (portal_id > 0 && portal_id <= 10) currentUbo = (*DescriptorHandle<StructuredBuffer<PortalUBO>>(pc.portalUbos))[0].data[portal_id - 1];

    if (currentUbo.fogParams.x < 0.5) {
        heapRWTex2D(pc.volumetricOut)[pixelCoords] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    float2 uv = (float2(pixelCoords) + 0.5) / float2(texSize);
    float depth = heapTex2D_r(pc.gbufferDepth).Load(int3(fullResCoords, 0)).x;

    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPosRaw = mul(currentUbo.invViewProj, ndc);
    float3 targetPos;
    if (abs(worldPosRaw.w) < 0.00001) {
        targetPos = currentUbo.camPos + normalize(worldPosRaw.xyz) * currentUbo.zFar;
    } else {
        targetPos = worldPosRaw.xyz / worldPosRaw.w;
    }

    float3 startPos = currentUbo.camPos;
    float3 rayDir = targetPos - startPos;
    float rayLength = length(rayDir);
    rayDir /= rayLength;

    float maxDist = min(rayLength, currentUbo.zFar);
    // Детальный реймарчинг (с локальными источниками и тенями) только для первых 120 метров
    float marchDist = min(maxDist, 120.0); 
    int STEPS = 50;
    float stepSize = marchDist / float(STEPS);
    
    float2 noiseUV = uv * (currentUbo.screenSize.xy / uint2(256, 256).xy);
    float dither = heapTex2D_r(pc.blueNoise).SampleLevel(heapSampler(pc.defaultSampler), noiseUV, 0.0).r;
    float3 currentPos = startPos + rayDir * (stepSize * dither);

    float3 accumulatedLight = float3(0.0);
    float transmittance = 1.0;
    float density = currentUbo.fogParams.y;
    
    float g = 0.6; 
    float cosThetaSun = dot(rayDir, -currentUbo.sunDir);
    float phaseSun = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosThetaSun, 1.5));

    for (int i = 0; i < STEPS; i++) {
        float h = max(currentPos.y - currentUbo.fogParams.w, 0.0);
        float currentDensity = density * exp(-h * currentUbo.fogParams.z);
        
        if (currentDensity > 0.001) {
            float3 stepLight = float3(0.0);
            
            float sunShadow = SunShadowVolumetric(currentPos, currentUbo);
            stepLight += currentUbo.sunColor * currentUbo.sunIntensity * sunShadow * phaseSun;
            
            // Базовый ambient: не даем туману в тени стать абсолютно черной дырой
            stepLight += currentUbo.fogColor.rgb * 0.1;

            float4 viewPos = mul(currentUbo.view, float4(currentPos, 1.0));
            float zView = max(-viewPos.z, currentUbo.zNear + 0.001); 
            uint cX = clamp(uint(float(fullResCoords.x) / (currentUbo.screenSize.x / float(currentUbo.gridDimX))), 0u, currentUbo.gridDimX - 1u);
            uint cY = clamp(uint(float(fullResCoords.y) / (currentUbo.screenSize.y / float(currentUbo.gridDimY))), 0u, currentUbo.gridDimY - 1u);
            uint cZ = clamp(uint(max(0.0, log(zView / currentUbo.zNear) * float(currentUbo.gridDimZ) / log(currentUbo.zFar / currentUbo.zNear))), 0u, currentUbo.gridDimZ - 1u);
            uint totalClusters = currentUbo.gridDimX * currentUbo.gridDimY * currentUbo.gridDimZ;
            uint clusterIdx = cX + cY * currentUbo.gridDimX + cZ * currentUbo.gridDimX * currentUbo.gridDimY;
            clusterIdx = clamp(clusterIdx, 0u, totalClusters - 1u);

            StructuredBuffer<LightGrid> lightGrid = DescriptorHandle<StructuredBuffer<LightGrid>>(pc.lightGrid);
            StructuredBuffer<uint> indexList = DescriptorHandle<StructuredBuffer<uint>>(pc.lightIndexList);
            StructuredBuffer<LightUBOBlock> lightBlock = DescriptorHandle<StructuredBuffer<LightUBOBlock>>(pc.lightBuffer);

            uint offset = lightGrid[clusterIdx].offset;
            uint lightsInCluster = min(lightGrid[clusterIdx].count, kMaxLights);
            int activeLights = max(lightBlock[0].activeLightsCount, 0);
            activeLights = min(activeLights, int(kMaxLights));

            for (uint l = 0; l < lightsInCluster; ++l) {
                uint listIdx = offset + 1u + l;
                if (listIdx >= kMaxLightIndexList) break;
                uint lightIndex = indexList[listIdx];
                lightIndex = min(lightIndex, uint(max(activeLights - 1, 0)));
                PointLightData light = lightBlock[0].lights[lightIndex];
                int type = int(light.posType.w);
                if (type == 4 || type == 0) continue;

                float3 L = light.posType.xyz - currentPos;
                float t = dot(L, rayDir); 
                
                float t_clamped = clamp(t, -stepSize * 0.5, stepSize * 0.5);
                float3 evalPos = currentPos + rayDir * t_clamped;
                float distToEval = distance(light.posType.xyz, evalPos);
                
                float radius = light.dirRadius.w;
                if (distToEval > radius) continue;

                float3 closestPoint = currentPos + rayDir * t;
                float D = distance(light.posType.xyz, closestPoint);
                float D_safe = max(D, 0.3); // Защита от бесконечных всплесков света
                
                float x_start = -stepSize * 0.5 - t;
                float x_end   =  stepSize * 0.5 - t;
                float integral = (atan(x_end / D_safe) - atan(x_start / D_safe)) / D_safe;
                
                float falloff = clamp(1.0 - pow((distToEval*distToEval) / (radius * radius), 2.0), 0.0, 1.0);
                float attenuation = (integral / stepSize) * (falloff * falloff);
                
                float3 L_dir = normalize(light.posType.xyz - evalPos);

                if (type == 2) {
                    float theta = dot(-L_dir, normalize(light.dirRadius.xyz));
                    float epsilon = light.shadowParams.x - light.shadowParams.y;
                    attenuation *= clamp((theta - light.shadowParams.y) / epsilon, 0.0, 1.0);
                }
                
                if (attenuation > 0.001) {
                    float shadow = 1.0;
                    if (int(light.shadowParams.z + 0.5) == 1) shadow = (type == 1) ? PointShadowVolumetric(evalPos, light, lightIndex) : SpotShadowVolumetric(evalPos, light, lightIndex);
                    float cosThetaL = dot(rayDir, L_dir);
                    float phaseL = (1.0 - g*g) / (4.0 * 3.14159 * pow(1.0 + g*g - 2.0*g*cosThetaL, 1.5));
                    stepLight += light.colorInt.xyz * light.colorInt.w * attenuation * shadow * phaseL;
                }
            }

            float ext = exp(-currentDensity * stepSize);
            // Стабильный, математически правильный физический интеграл
            float3 S = stepLight * 0.8 * transmittance * (1.0 - ext);
            accumulatedLight += S;
            transmittance *= ext;
            if (transmittance < 0.01) break;
        }
        currentPos += rayDir * stepSize;
    }
    
    // Аналитическое продолжение тумана для дальних объектов (без деталей теней для оптимизации)
    if (maxDist > marchDist && transmittance > 0.01) {
        float remainDist = maxDist - marchDist;
        float3 midPos = currentPos + rayDir * (remainDist * 0.5);
        float h = max(midPos.y - currentUbo.fogParams.w, 0.0);
        float currentDensity = density * exp(-h * currentUbo.fogParams.z);
        
        if (currentDensity > 0.001) {
            float3 stepLight = currentUbo.sunColor * currentUbo.sunIntensity * phaseSun * 0.8; // Смягченное солнце
            stepLight += currentUbo.fogColor.rgb * 0.4; // Повышенный амбиент вдалеке
            
            float ext = exp(-currentDensity * remainDist);
            float3 S = stepLight * 0.8 * transmittance * (1.0 - ext);
            accumulatedLight += S;
            transmittance *= ext;
        }
    }

    float3 finalFog = accumulatedLight + currentUbo.inscatterColor.rgb * (1.0 - transmittance) * 0.1;
    heapRWTex2D(pc.volumetricOut)[pixelCoords] = float4(finalFog, transmittance);
}
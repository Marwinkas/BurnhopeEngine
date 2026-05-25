import common.SlangBuiltins;
import common.Bindless;
import common.GlobalUbo;
import common.SceneGpu;
import common.RayTrace;

struct ProbeUpdateHeapPC {
    uint globalUbo;
    uint giCascade0;
    uint outProbeTex;
    uint objectStorage;
    uint materialStorage;
    uint rtTlas;
    uint defaultSampler;
    float4 probeGridMin;
    float4 probeGridMax;
    int4 probeCount;
    float4 params;
};

[[vk::push_constant]] ProbeUpdateHeapPC pc;



struct MaterialData {
    int albedoAlphaIdx; int normalIdx; int ormxIdx; int emissiveIdx;
    int useTriplanar; int isTransparent; int repeatTexture; int pad1;
    
    float2 uvScale; float triplanarScale; float emissiveIntensity;
    
    float4 albedoColor;
    float4 emissiveColor;

    float metallicStrength;
    float roughnessStrength;
    float normalStrength;
    float heightStrength;

    float aoStrength;
    float pad2; float pad3; float pad4;
};


// Bindless текстуры


float4 unpackQTangent(uint packedVal) {
    int ix = (int(packedVal) << 22) >> 22;
    int iy = (int(packedVal) << 12) >> 22;
    int iz = (int(packedVal) << 2)  >> 22;
    int iw = (int(packedVal) >> 30);
    return float4(
        max(float(ix) / 511.0, -1.0), max(float(iy) / 511.0, -1.0),
        max(float(iz) / 511.0, -1.0), max(float(iw) / 1.0, -1.0)
    );
}
float3 getNormalFromQTangent(uint packedVal) {
    float4 q = unpackQTangent(packedVal);
    float qx = q.x; float qy = q.y; float qz = q.z;
    float qw = sqrt(max(0.0, 1.0 - qx*qx - qy*qy - qz*qz));
    return float3(2.0*qx*qz + 2.0*qy*qw, 2.0*qy*qz - 2.0*qx*qw, 1.0 - 2.0*qx*qx - 2.0*qy*qy);
}

float2 unpackUV(uint uvPacked) {
    return unpackHalf2x16(uvPacked);
}

// Декодирование направления луча из октаэдра
float3 octaDecode(float2 uv) {
    uv = uv * 2.0 - 1.0;
    float3 v = float3(uv.x, uv.y, 1.0 - abs(uv.x) - abs(uv.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * sign(v.xy);
    return normalize(v);
}

// Полезная функция для защиты от математического мусора
bool isNaN(float val) {
    return (val < 0.0 || 0.0 < val || val == 0.0) ? false : true;
}
bool isAnyNaN(float3 v) {
    return isNaN(v.x) || isNaN(v.y) || isNaN(v.z);
}

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

float3 EvaluateHit(int objIndex, int primID, float2 bary, float3 hitPos) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    StructuredBuffer<ObjectData> objBuf = DescriptorHandle<StructuredBuffer<ObjectData>>(pc.objectStorage);
    ObjectData obj = objBuf[objIndex];
    StructuredBuffer<uint> indices = heapStructured<uint>(obj.indexHeap);
    StructuredBuffer<PackedVertexAttr> attributes = heapStructured<PackedVertexAttr>(obj.attrHeap);
    
    uint triBase = uint(primID) * 3u;
    uint i0 = indices[triBase + 0];
    uint i1 = indices[triBase + 1];
    uint i2 = indices[triBase + 2];
    
    PackedVertexAttr v0_raw = attributes[i0];
    PackedVertexAttr v1_raw = attributes[i1];
    PackedVertexAttr v2_raw = attributes[i2];
    
    float u = bary.x;
    float v = bary.y;
    float w = clamp(1.0 - u - v, 0.0, 1.0); // Защита от выхода за пределы
    
    float2 finalUV = unpackUV(v0_raw.texUV) * w + unpackUV(v1_raw.texUV) * u + unpackUV(v2_raw.texUV) * v;
    float3 localNormal = getNormalFromQTangent(v0_raw.qTangent) * w + getNormalFromQTangent(v1_raw.qTangent) * u + getNormalFromQTangent(v2_raw.qTangent) * v;
    
    float3x3 normalMatrix = inverse3x3(float3x3(obj.modelMatrix));
    float3 worldNormal = normalize(mul(normalMatrix, localNormal));
    
    StructuredBuffer<MaterialData> matBuf = DescriptorHandle<StructuredBuffer<MaterialData>>(pc.materialStorage);
    MaterialData mat = matBuf[obj.materialID];
    SamplerState sam = heapSampler(pc.defaultSampler);
    
static const float PI = 3.14159265359;

    // 1. float3(1.0) - это идеально белый (как снег). Серый - это 0.5 или 0.8
    float3 albedo = float3(0.8); 
    float hitAO = mat.aoStrength;
    if (mat.albedoAlphaIdx >= 0) {
        albedo = heapTex2D(uint(mat.albedoAlphaIdx)).SampleLevel(sam, finalUV * mat.uvScale, 0.0).rgb * mat.albedoColor.rgb;
    } else {
        albedo = mat.albedoColor.rgb;
    }
    if (mat.ormxIdx >= 0) {
        float4 ormx = heapTex2D(uint(mat.ormxIdx)).SampleLevel(sam, finalUV * mat.uvScale, 0.0);
        hitAO *= ormx.r;
    }

    float3 L = normalize(-ubo.sunDir); 
    float NdotL = max(0.0, dot(worldNormal, L));
    
    // ПУСКАЕМ ТЕНЕВОЙ ЛУЧ К СОЛНЦУ
    float shadow = 1.0; 
    if (NdotL > 0.0) {
        float3 shadowOrigin = hitPos + worldNormal * 0.05;
        if (rtTraceShadow(heapTlas(pc.rtTlas), shadowOrigin, L, 1000.0)) {
            shadow = 0.0;
        }
    }
    
    // 2. Полностью убираем фейковый эмбиент из GI! 
    // Если объект в глухой тени (в коробке), он не должен светиться сам по себе.
    // Настоящий свет неба придет сюда через лучи, вылетевшие наружу!
    
    float3 actualSunRadiance = ubo.sunColor * ubo.sunIntensity;
    
    // 3. ФИЗИЧЕСКИ КОРРЕКТНЫЙ РАСЧЕТ (Делим albedo на PI)
    float3 directLight = (albedo / PI) * actualSunRadiance * NdotL * shadow * lerp(1.0, hitAO, 0.3);
    
    float3 finalColor = directLight;
    
    if (isAnyNaN(finalColor)) return float3(0.0);
    return finalColor;
}

float4 rtRaymarch(float3 origin, float3 dir, float rayLen) {
    RtHitInfo hit;
    if (rtTraceOpaque(heapTlas(pc.rtTlas), origin, dir, rayLen, hit)) {
        float3 hitPos = origin + dir * hit.t;
        int objIndex = int(hit.instanceCustomIndex + hit.geometryIndex);
        float3 hitColor = EvaluateHit(objIndex, int(hit.primitiveIndex), hit.barycentrics, hitPos);
        return float4(hitColor, 1.0);
    }

    // ПРОМАХ (Луч ни во что не врезался).
    // Мы рисуем небо ТОЛЬКО на самом последнем (самом длинном) каскаде!
    // Иначе короткие лучи будут плюсовать небо на каждом шаге, и вся комната станет синей и яркой.
    if (pc.params.y > 0.5) {
        return float4(CalculateSky(loadGlobalUbo(pc.globalUbo), dir), 0.0);
    }
    return float4(0.0); 
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);

    int2 pixel = int2(id.xy);
    RWTexture2D<float4> outProbe = heapRWTex2D(pc.outProbeTex);
    uint2 probeTexSize;
    outProbe.GetDimensions(probeTexSize.x, probeTexSize.y);
    if (pixel.x >= int(probeTexSize.x) || pixel.y >= int(probeTexSize.y)) return;

    int octaSize = int(pc.params.w);
    int3 pCount = pc.probeCount.xyz;
    
    int probeIdxX  = pixel.x / octaSize;
    int probeIdxYZ = pixel.y / octaSize;
    int probeIdxY  = probeIdxYZ % pCount.y;
    int probeIdxZ  = probeIdxYZ / pCount.y;

    if (probeIdxX >= pCount.x || probeIdxZ >= pCount.z) return;

    float3 gridSize = pc.probeGridMax.xyz - pc.probeGridMin.xyz;
    float3 probePos = pc.probeGridMin.xyz + gridSize * (float3(probeIdxX, probeIdxY, probeIdxZ) + 0.5) / float3(pCount);

    float jitter = frac(sin(dot(float3(probeIdxX, probeIdxY, probeIdxZ), float3(12.9898, 78.233, 45.164))) * 43758.5453);
    
    float2 localUV = (float2(pixel % octaSize) + 0.5) / float(octaSize);
    float3 rayDir = octaDecode(localUV);
    float startOffset = (pc.probeCount.w > 0) ? pc.params.x * 0.5 : 0.0;
    float currentRange = pc.params.x - startOffset;

    float4 result = rtRaymarch(probePos + rayDir * startOffset, rayDir, currentRange);
    
    outProbe[pixel] = result;
}
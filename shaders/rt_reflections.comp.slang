import common.SlangBuiltins;
import common.Bindless;
import common.GlobalUbo;
import common.SceneGpu;
import common.RayTrace;

struct RtReflectionHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferDepth;
    uint rtTlas;
    uint objectStorage;
    uint materialStorage;
    uint giDiffuse;
    uint rtReflections;
    uint defaultSampler;
};

struct RtProbeData {
    float4 positionAndRadius;
};

struct ProbesInfo {
    int count;
    RtProbeData data[16];
};

[[vk::push_constant]] RtReflectionHeapPC pc;

static ProbesInfo probes;





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


static const float PI = 3.14159265359;

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

float2 octaEncode(float3 dir) {
    dir = normalize(dir);
    dir /= (abs(dir.x) + abs(dir.y) + abs(dir.z));
    if (dir.z < 0.0) dir.xy = (1.0 - abs(dir.yx)) * sign(dir.xy);
    return dir.xy * 0.5 + 0.5;
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

float3 SampleProbe(float3 pos, float3 dir) {
    return CalculateSky(loadGlobalUbo(pc.globalUbo), dir);
}

float3 sampleRadiance(float3 worldPos, float3 N, float3 dir) {
    return float3(0.0);
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadGlobalUbo(pc.globalUbo);
    SamplerState sam = heapSampler(pc.defaultSampler);
    Texture2D<float> gDepth = heapTex2D_r(pc.gbufferDepth);
    Texture2D<float4> gNormalRoughness = heapTex2D(pc.gbufferNormal);
    RWTexture2D<float4> outReflections = heapRWTex2D(pc.rtReflections);
    RaytracingAccelerationStructure tlas = heapTlas(pc.rtTlas);
    StructuredBuffer<MaterialData> matBuf = DescriptorHandle<StructuredBuffer<MaterialData>>(pc.materialStorage);

    int2 pixel = int2(id.xy);
    if (pixel.x >= int(ubo.screenSize.x) || pixel.y >= int(ubo.screenSize.y)) return;

    float2 uv = (float2(pixel) + 0.5) / ubo.screenSize.xy;
    float depth = gDepth.SampleLevel(sam, uv, 0).x;
    if (depth >= 1.0 || ubo.rtParams.x < 0.5) {
        outReflections[pixel] = float4(0.0);
        return;
    }

    float4 normRough = gNormalRoughness.SampleLevel(sam, uv, 0);
    float3 N = normalize(normRough.rgb);
    float roughness = normRough.a;
    float metallic = 0.0;

    if (roughness > 0.6 && metallic < 0.1) {
        outReflections[pixel] = float4(0.0);
        return;
    }

    float4 ndc = float4(uv.x * 2.0 - 1.0, uv.y * 2.0 - 1.0, depth, 1.0);
    float4 worldPosRaw = mul(ubo.invViewProj, ndc);
    float3 worldPos = worldPosRaw.xyz / worldPosRaw.w;
    
    float3 V = normalize(ubo.camPos - worldPos);
    float3 R = reflect(-V, N);

    float3 rayOrigin = worldPos + N * 0.05;
    float3 rayDir = R;
    float3 accumulatedColor = float3(0.0);
    float3 throughput = float3(1.0);
    int MAX_BOUNCES = int(ubo.rtParams.y); // Берем кол-во отскоков из UI

    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++) {
        RtHitInfo hit;
        if (rtTraceOpaque(tlas, rayOrigin, rayDir, 100.0, hit)) {
            float u = hit.barycentrics.x;
            float v = hit.barycentrics.y;
            float w = 1.0 - u - v;
            
            StructuredBuffer<ObjectData> objBuf = DescriptorHandle<StructuredBuffer<ObjectData>>(pc.objectStorage);
            uint objIdx = hit.instanceCustomIndex + hit.geometryIndex;
            if (objIdx >= 65536u) {
                break;
            }
            ObjectData obj = objBuf[objIdx];
            if (obj.indexHeap == 0u || obj.attrHeap == 0u) {
                break;
            }
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
            float3 localNormal = getNormalFromQTangent(v0_raw.qTangent) * w + getNormalFromQTangent(v1_raw.qTangent) * u + getNormalFromQTangent(v2_raw.qTangent) * v;
            float3x3 normalMatrix = inverse3x3(float3x3(obj.modelMatrix));
            float3 worldNormal = normalize(mul(normalMatrix, localNormal));
            if (dot(worldNormal, rayDir) > 0.0) worldNormal = -worldNormal;

            if (mat.albedoAlphaIdx >= 0) {
                albedo = heapTex2D(uint(mat.albedoAlphaIdx)).SampleLevel(sam, texUV * mat.uvScale, 0).rgb * mat.albedoColor.rgb;
            } else {
                albedo = mat.albedoColor.rgb;
            }
            
            float hitRoughness = mat.roughnessStrength;
            float hitMetallic = mat.metallicStrength;
            float hitAO = mat.aoStrength;
            if (mat.ormxIdx >= 0) {
                float4 ormx = heapTex2D(uint(mat.ormxIdx)).SampleLevel(sam, texUV * mat.uvScale, 0);
                hitAO *= ormx.r; hitRoughness *= ormx.g; hitMetallic *= ormx.b;
            }
            
            float3 hitPos = rayOrigin + rayDir * hit.t;
            float3 L = normalize(-ubo.sunDir);
            float NdotL = max(dot(worldNormal, L), 0.0);
            float shadow = 1.0;
            if (NdotL > 0.0) {
                if (rtTraceShadow(tlas, hitPos + worldNormal * 0.02 + L * 0.02, L, 1000.0)) {
                    shadow = 0.0;
                }
            }
            
            float3 directLight = (albedo / PI) * ubo.sunColor * ubo.sunIntensity * (NdotL * shadow) * lerp(1.0, hitAO, 0.3);
            
            // --- Radiance Cascades PBR GI в отражениях ---
            float3 tangent = normalize(abs(worldNormal.x) > 0.9 ? float3(0, 1, 0) : cross(float3(1, 0, 0), worldNormal));
            float3 bitangent = cross(worldNormal, tangent);
            float3 diffuseGI = float3(0.0);
            const int NUM_DIFFUSE = 4; // 4 луча достаточно для RT отражений
            for (int d = 0; d < NUM_DIFFUSE; d++) {
                float i_f = float(d) + 0.5;
                float phi = 2.39996323 * i_f;
                float cosTheta = sqrt(1.0 - i_f / float(NUM_DIFFUSE));
                float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
                float3 d_dir = (cos(phi) * sinTheta) * tangent + (sin(phi) * sinTheta) * bitangent + cosTheta * worldNormal;
                diffuseGI += sampleRadiance(hitPos, worldNormal, d_dir);
            }
            diffuseGI /= float(NUM_DIFFUSE);

        float giStrength = 1.0; // Такое же значение, как в lighting.comp
            float3 F0_hit = lerp(float3(0.04), albedo, hitMetallic);
            float3 F_hit = fresnelSchlick(max(dot(worldNormal, -rayDir), 0.0), F0_hit);
            float3 kD_hit = (float3(1.0) - F_hit) * (1.0 - hitMetallic);
            
            float3 indirectLight = diffuseGI * giStrength * albedo * kD_hit * hitAO;
            float3 hitColor = directLight + indirectLight;

            if (ubo.fogParams.x > 0.5) {
                float fogAmount = clamp(exp(-hit.t * ubo.fogParams.y), 0.0, 1.0);
                hitColor = lerp(ubo.fogColor.rgb, hitColor, fogAmount);
            }

            // Если это зеркало и мы можем еще раз отскочить
             if (hitRoughness < 0.4 && hitMetallic > 0.1) {
                float3 F0 = lerp(float3(0.04), albedo, hitMetallic);
                float3 F = fresnelSchlick(max(dot(worldNormal, -rayDir), 0.0), F0);
                
                accumulatedColor += throughput * hitColor * (1.0 - hitMetallic);
                throughput *= F;
                
                if (bounce == MAX_BOUNCES - 1) {
                    // Если это последний отскок - дальше трассировать не можем, берем цвет отражения из Local Probe
                    accumulatedColor += throughput * SampleProbe(hitPos, reflect(rayDir, worldNormal));
                } else {
                    // Готовим следующий луч
                    rayOrigin = hitPos + worldNormal * 0.02;
                    rayDir = reflect(rayDir, worldNormal);
                }
            } else {
                accumulatedColor += throughput * hitColor;
                break;
            }
        } else {
            accumulatedColor += throughput * SampleProbe(rayOrigin, rayDir);
            break;
        }
    }

    outReflections[pixel] = float4(accumulatedColor, 1.0);
}
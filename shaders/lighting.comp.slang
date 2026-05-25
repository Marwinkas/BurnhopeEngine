// Deferred lighting — aligned with gbuffer.frag (BDA GlobalUbo, bindless heap, G-Buffer Load).
import common.Bindless;
import common.GlobalUbo;
import common.LightTypes;
import common.SceneAddresses;

struct LightingHeapPC {
    uint globalUbo;
    uint gbufferNormal;
    uint gbufferAlbedo;
    uint gbufferHeightAo;
    uint gbufferDepth;
    uint gbufferEmissive;
    uint gbufferPortalId;
    uint shadowCsm;
    uint shadowAtlas;
    uint blueNoise;
    uint lightBuffer;
    uint lightGrid;
    uint lightIndexList;
    uint faceMatrices;
    uint decalBuffer;
    uint hdrOutput;
    uint giDiffuse;
    uint giSpecular;
    uint gtaoOutput;
    uint rtReflections;
    uint vsmAtlas;
    uint vsmPageTable;
    uint portalUbos;
    uint volumetric;
    uint defaultSampler;
    uint sceneAddressesBdaLo;
    uint sceneAddressesBdaHi;
};

[[vk::push_constant]] LightingHeapPC pc;

uint64_t lightingSceneBda() {
    return (uint64_t(pc.sceneAddressesBdaHi) << 32u) | uint64_t(pc.sceneAddressesBdaLo);
}

static const float kPi = 3.14159265359;
static const float kSkyDepthThreshold = 0.9995;
static const uint kMaxLights = 100u;
static const uint kMaxLightIndexList = 65536u;

// 0 = full, 1 = sun+PBR, 2 = +lights, 3 = albedo blit, 4 = depth viz, 99 = magenta
static const int kLightingDebugMode = 4;
// NV: heap UBO, CSM depth array, portal R8_UINT heap load can DEVICE_LOST.
static const bool kLightingUseHeapUboFallback = false;
static const bool kLightingEnableSunShadow = false;
static const bool kLightingEnableLightCluster = false;
static const bool kLightingEnablePortal = false;
// Hardware depth (D24/D32) via bindless Sample/Load → DEVICE_LOST on RTX; use emissive.a from gbuffer.
static const bool kLightingSampleHardwareDepth = false;

#define G_NORMAL heapTex2D(pc.gbufferNormal)
#define G_ALBEDO heapTex2D(pc.gbufferAlbedo)
#define G_HEIGHT heapTex2D(pc.gbufferHeightAo)
#define G_DEPTH heapTex2D_r(pc.gbufferDepth)
#define G_EMISSIVE heapTex2D(pc.gbufferEmissive)
#define G_PORTAL heapTex2D_u(pc.gbufferPortalId)
#define SUN_SHADOW heapTex2DArray(pc.shadowCsm)
#define HDR_OUT heapRWTex2D(pc.hdrOutput)
#define GTAO_IMG heapRWTex2D_r(pc.gtaoOutput)
#define GI_DIFF heapTex2D(pc.giDiffuse)
#define GI_SPEC heapTex2D(pc.giSpecular)
#define RT_REFL heapRWTex2D(pc.rtReflections)
#define SAM heapSampler(pc.defaultSampler)

float readSceneDepth(float2 uv, int2 px) {
    if (kLightingSampleHardwareDepth && pc.gbufferDepth != 0u) {
        return G_DEPTH.SampleLevel(SAM, uv, 0).x;
    }
    return G_EMISSIVE.SampleLevel(SAM, uv, 0).a;
}

GlobalUbo loadLightingUbo() {
    uint64_t sceneBda = lightingSceneBda();
    if (sceneBda != 0u) {
        SceneAddresses scene = loadSceneAddresses(sceneBda);
        if (scene.globalUboBda != 0u) {
            return *sceneGlobalUboPtr(scene);
        }
    }
    if (kLightingUseHeapUboFallback && pc.globalUbo != 0u) {
        return loadGlobalUbo(pc.globalUbo);
    }
    return (GlobalUbo)0;
}

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    return a2 / (kPi * pow(NdotH * NdotH * (a2 - 1.0) + 1.0, 2.0));
}

float GeometrySchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

float3 fresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 evalPBR(float3 N, float3 V, float3 L, float3 albedo, float metallic, float roughness,
               float3 radiance, float ao) {
    float3 H = normalize(V + L);
    float3 F0 = lerp(float3(0.04), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    float3 kS = F;
    float3 kD = (float3(1.0) - kS) * (1.0 - metallic);
    float3 spec = (NDF * G * F) / (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001);
    float NdotL = max(dot(N, L), 0.0);
    float directAO = lerp(1.0, ao, 0.3);
    return (kD * albedo / kPi + spec) * radiance * NdotL * directAO;
}

float3 decodeNormal(float4 packed) {
    float3 N = packed.rgb * 2.0 - 1.0;
    if (length(N) < 0.1) {
        return float3(0.0, 1.0, 0.0);
    }
    return normalize(N);
}

float3 reconstructWorldPos(float2 uv, float depth, GlobalUbo ubo) {
    float4 ndc = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 w = mul(ubo.invViewProj, ndc);
    return w.xyz / max(w.w, 1e-6);
}

float3 calculateSky(float3 viewDir, GlobalUbo ubo) {
    float t = max(viewDir.y, 0.0);
    float3 sky = lerp(ubo.skyHorizonColor.rgb, ubo.skyZenithColor.rgb, pow(t, 0.5));
    if (ubo.fogParams.x > 0.5) {
        sky = lerp(ubo.fogColor.rgb, sky, pow(t, 0.5));
    }
    float sunDot = dot(viewDir, -ubo.sunDir);
    float sunDisk = smoothstep(1.0 - ubo.skySunParams.x - 0.001, 1.0 - ubo.skySunParams.x, sunDot);
    float sunAura = pow(max(sunDot, 0.0), 1.0 / max(ubo.skySunParams.z, 0.001)) * ubo.skySunParams.y;
    float3 sunCol = ubo.sunColor * ubo.sunIntensity;
    sky += sunCol * sunDisk;
    sky += sunCol * sunAura * 0.1;
    return sky;
}

float getGtao(int2 px, float depth, GlobalUbo ubo) {
    if (ubo.gtaoParams.x < 0.5 || pc.gtaoOutput == 0u) {
        return 1.0;
    }
    int2 halfRes = int2(ubo.screenSize.xy * 0.5);
    float2 uv = (float2(px) + 0.5) / ubo.screenSize.xy;
    float2 p = uv * float2(halfRes) - 0.5;
    int2 i = int2(p);
    float2 f = frac(p);
    float cLin = (ubo.zNear * ubo.zFar) / (ubo.zFar + depth * (ubo.zNear - ubo.zFar));
    float w00 = (1.0 - f.x) * (1.0 - f.y);
    float w10 = f.x * (1.0 - f.y);
    float w01 = (1.0 - f.x) * f.y;
    float w11 = f.x * f.y;
    float ao = 0.0;
    ao += GTAO_IMG[clamp(i, int2(0), halfRes - 1)].r * w00;
    ao += GTAO_IMG[clamp(i + int2(1, 0), int2(0), halfRes - 1)].r * w10;
    ao += GTAO_IMG[clamp(i + int2(0, 1), int2(0), halfRes - 1)].r * w01;
    ao += GTAO_IMG[clamp(i + int2(1, 1), int2(0), halfRes - 1)].r * w11;
    return clamp(ao, 0.0, 1.0);
}

float sunShadow(float3 worldPos, float3 N, float3 L, GlobalUbo ubo) {
    if (!kLightingEnableSunShadow || pc.shadowCsm == 0u) {
        return 0.0;
    }
    float NdotL = max(dot(N, L), 0.0);
    float3 biasN = N * (0.005 + 0.01 * (1.0 - NdotL));
    float4 viewPos = mul(ubo.view, float4(worldPos + biasN, 1.0));
    float depthView = max(-viewPos.z, ubo.zNear + 0.001);
    int layer = 0;
    for (int i = 0; i < 3; ++i) {
        if (depthView > ubo.cascadeSplits[i]) {
            layer = i + 1;
        }
    }
    layer = clamp(layer, 0, 3);
    float4 ls = mul(ubo.sunLightSpaceMatrices[layer], float4(worldPos + biasN, 1.0));
    float3 proj = ls.xyz / ls.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    proj.y = 1.0 - proj.y;
    if (proj.z > 1.0 || any(proj.xy < 0.0) || any(proj.xy > 1.0)) {
        return 0.0;
    }
    float shadow = 0.0;
    float2 texel = 1.0 / float2(2048.0, 2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float d = SUN_SHADOW.SampleLevel(SAM, float3(proj.xy + float2(x, y) * texel, float(layer)), 0.0).r;
            shadow += proj.z - 0.002 > d ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

void accumulateLight(
    inout float3 result,
    Light light,
    float3 worldPos,
    float3 N,
    float3 V,
    float3 albedo,
    float metallic,
    float roughness,
    float ao,
    GlobalUbo ubo) {
    float3 L = float3(0.0);
    float atten = 1.0;
    if (light.type == 0) {
        L = normalize(-light.direction);
    } else if (light.type == 1) {
        float3 toL = light.position - worldPos;
        float dist = length(toL);
        L = toL / max(dist, 1e-4);
        float r2 = light.radius * light.radius;
        float fall = clamp(1.0 - pow((dist * dist) / max(r2, 1e-4), 2.0), 0.0, 1.0);
        atten = (fall * fall) / (dist * dist + 0.0001);
    } else if (light.type == 2) {
        float3 toL = light.position - worldPos;
        float dist = length(toL);
        L = toL / max(dist, 1e-4);
        float r2 = light.radius * light.radius;
        float fall = clamp(1.0 - pow((dist * dist) / max(r2, 1e-4), 2.0), 0.0, 1.0);
        atten = (fall * fall) / (dist * dist + 0.0001);
        float3 spotDir = normalize(light.direction);
        float theta = dot(L, -spotDir);
        float outer = cos(radians(light.outerCone));
        float inner = cos(radians(light.innerCone));
        atten *= smoothstep(outer, inner, theta);
    } else {
        return;
    }
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) {
        return;
    }
    float shadow = 0.0;
    if (light.castShadows == 1) {
        if (light.type == 0) {
            shadow = sunShadow(worldPos, N, L, ubo);
        }
    }
    float3 rad = light.color * light.intensity * atten;
    float3 pbr = evalPBR(N, V, L, albedo, metallic, roughness, rad, ao);
    result += pbr * (1.0 - shadow);
}

Light lightFromGpu(PointLightData raw, int lightIndex) {
    Light light;
    light.type = int(raw.posType.w);
    light.position = raw.posType.xyz;
    light.color = raw.colorInt.xyz;
    light.intensity = raw.colorInt.w;
    light.direction = raw.dirRadius.xyz;
    light.radius = raw.dirRadius.w;
    light.innerCone = raw.shadowParams.x;
    light.outerCone = raw.shadowParams.y;
    light.castShadows = int(raw.shadowParams.z + 0.5);
    light.shadowSlot = int(raw.shadowParams.w + 0.5);
    light.lightSpaceMatrix = raw.lightSpaceMatrix;
    light.lightIndex = lightIndex;
    return light;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    GlobalUbo ubo = loadLightingUbo();

    int2 px = int2(id.xy);
    if (px.x >= int(ubo.screenSize.x) || px.y >= int(ubo.screenSize.y)) {
        return;
    }

    float2 uv = (float2(px) + 0.5) / ubo.screenSize.xy;

    if (kLightingDebugMode == 99) {
        HDR_OUT[px] = float4(1.0, 0.0, 1.0, 1.0);
        return;
    }
    if (kLightingDebugMode == 3) {
        float4 alb = G_ALBEDO.Load(int3(px, 0));
        HDR_OUT[px] = float4(alb.rgb, 1.0);
        return;
    }
    if (kLightingDebugMode == 4) {
        float d = readSceneDepth(uv, px);
        HDR_OUT[px] = float4(d, d, d, 1.0);
        return;
    }

    float depth = readSceneDepth(uv, px);
    bool isSky = depth >= kSkyDepthThreshold;

    if (isSky) {
        float4 ndcSky = float4(uv * 2.0 - 1.0, 1.0, 1.0);
        float4 wSky = mul(ubo.invViewProj, ndcSky);
        float3 viewDir = normalize(wSky.xyz / max(wSky.w, 1e-6) - ubo.camPos);
        HDR_OUT[px] = float4(calculateSky(viewDir, ubo), 1.0);
        return;
    }

    float4 normRough = G_NORMAL.Load(int3(px, 0));
    float4 albMet = G_ALBEDO.Load(int3(px, 0));
    float4 heightAO = G_HEIGHT.Load(int3(px, 0));
    float4 emissivePacked = G_EMISSIVE.Load(int3(px, 0));
    float3 emissive = emissivePacked.rgb;

    float3 N = decodeNormal(normRough);
    float roughness = clamp(normRough.a, 0.04, 1.0);
    float3 albedo = albMet.rgb;
    float metallic = albMet.a;
    float ao = heightAO.g;

    float3 worldPos = reconstructWorldPos(uv, depth, ubo);
    if (any(isnan(worldPos)) || any(isinf(worldPos))) {
        HDR_OUT[px] = float4(albedo, 1.0);
        return;
    }

    if (kLightingEnablePortal && pc.portalUbos != 0u && pc.gbufferPortalId != 0u) {
        uint portalId = G_PORTAL.Load(int3(px, 0)).x;
        if (portalId > 0u && portalId <= 10u) {
            ubo = heapStructured<PortalUBO>(pc.portalUbos)[0].data[portalId - 1u];
        }
    }

    float3 V = normalize(ubo.camPos - worldPos);
    float gtao = getGtao(px, depth, ubo);
    float cavity = clamp(heightAO.r * 0.5 + 0.9, 0.0, 1.0);

    float3 direct = float3(0.0);

    // Sun (always from GlobalUbo)
    {
        float3 sunL = normalize(-ubo.sunDir);
        float sunNdotL = max(dot(N, sunL), 0.0);
        if (sunNdotL > 0.0) {
            float3 sunRad = ubo.sunColor * ubo.sunIntensity;
            float3 pbr = evalPBR(N, V, sunL, albedo, metallic, roughness, sunRad, ao);
            float sh = sunShadow(worldPos, N, sunL, ubo);
            direct += pbr * (1.0 - sh);
        }
    }

    if (kLightingDebugMode == 1) {
        HDR_OUT[px] = float4(direct + float3(0.03) * albedo * ao * gtao + emissive, 1.0);
        return;
    }

    int activeLights = 0;
    if (pc.lightBuffer != 0u && kLightingDebugMode != 1) {
        StructuredBuffer<LightUBOBlock> lightBlock = heapStructured<LightUBOBlock>(pc.lightBuffer);
        activeLights = clamp(lightBlock[0].activeLightsCount, 0, int(kMaxLights));

    // Clustered lights (when light culling ran)
    bool usedCluster = false;
    if (kLightingEnableLightCluster && pc.lightGrid != 0u && pc.lightIndexList != 0u && activeLights > 0) {
        float4 viewPos = mul(ubo.view, float4(worldPos, 1.0));
        float zView = max(-viewPos.z, ubo.zNear + 0.001);
        uint gridX = max(ubo.gridDimX, 1u);
        uint gridY = max(ubo.gridDimY, 1u);
        uint gridZ = max(ubo.gridDimZ, 1u);
        uint cx = uint(float(px.x) / (ubo.screenSize.x / float(gridX)));
        uint cy = uint(float(px.y) / (ubo.screenSize.y / float(gridY)));
        float zRatio = clamp(zView / ubo.zNear, 1.0, ubo.zFar / max(ubo.zNear, 1e-6));
        float logDenom = max(log(ubo.zFar / max(ubo.zNear, 1e-6)), 1e-6);
        uint cz = uint(clamp(log(zRatio) * float(gridZ) / logDenom, 0.0, float(gridZ - 1u)));
        cx = clamp(cx, 0u, gridX - 1u);
        cy = clamp(cy, 0u, gridY - 1u);
        cz = clamp(cz, 0u, gridZ - 1u);
        uint clusterIdx = cx + cy * gridX + cz * gridX * gridY;
        uint totalClusters = gridX * gridY * gridZ;
        clusterIdx = clamp(clusterIdx, 0u, totalClusters - 1u);
        uint offset = heapStructured<LightGrid>(pc.lightGrid)[clusterIdx].offset;
        uint count = min(heapStructured<LightGrid>(pc.lightGrid)[clusterIdx].count, kMaxLights);
        if (count > 0u) {
            usedCluster = true;
            for (uint i = 0u; i < count; ++i) {
                uint listIdx = offset + 1u + i;
                if (listIdx >= kMaxLightIndexList) {
                    break;
                }
                uint li = heapStructured<uint>(pc.lightIndexList)[listIdx];
                li = min(li, uint(max(activeLights - 1, 0)));
                Light light = lightFromGpu(lightBlock[0].lights[li], int(li));
                accumulateLight(direct, light, worldPos, N, V, albedo, metallic, roughness, ao, ubo);
            }
        }
    }

        // Minimal / uncull fallback: iterate all active lights when cluster list is empty
        if (!usedCluster && activeLights > 0) {
            for (int i = 0; i < activeLights; ++i) {
                Light light = lightFromGpu(lightBlock[0].lights[i], i);
                accumulateLight(direct, light, worldPos, N, V, albedo, metallic, roughness, ao, ubo);
            }
        }
    }

    float3 ambient = float3(0.03) * albedo * ao * gtao * cavity;
    float3 finalColor = direct + ambient + emissive;

    // Optional GI / RT (full path)
    float rcEnable = ubo.rtParams.z;
    if (rcEnable > 0.5 && pc.giDiffuse != 0u) {
        float3 diffGi = GI_DIFF.Load(int3(px, 0)).rgb;
        float3 specGi = GI_SPEC.Load(int3(px, 0)).rgb;
        float3 F0 = lerp(float3(0.04), albedo, metallic);
        float3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);
        float3 kD = (float3(1.0) - F) * (1.0 - metallic);
        finalColor += diffGi * albedo * kD * ao * gtao;
        finalColor += specGi * F * ao * gtao;
    }
    if (ubo.rtParams.x > 0.5 && pc.rtReflections != 0u) {
        float reflMask = smoothstep(0.6, 0.2, roughness);
        float3 F0 = lerp(float3(0.04), albedo, metallic);
        float3 F = fresnelSchlick(max(dot(N, V), 0.0), F0);
        finalColor += RT_REFL[px].rgb * F * reflMask;
    }

    if (any(isnan(finalColor)) || any(isinf(finalColor))) {
        finalColor = albedo;
    }

    HDR_OUT[px] = float4(finalColor, 1.0);
}

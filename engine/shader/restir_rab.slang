#define RTXDI_ENABLE_PRESAMPLING 1
#define RTXDI_ALLOWED_BIAS_CORRECTION 3

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "Rtxdi/DI/ReSTIRDIParameters.h"
#include "Rtxdi/Utils/RandomSamplerState.hlsli"

struct RestirSlot {
    uint lightData;
    uint uvData;
    uint mVisibility;
    uint distanceAge;
    float targetPdf;
    float weight;
    uint pad0;
    uint pad1;
};

struct RestirConstants {
    RTXDI_RuntimeParameters runtime;
    RTXDI_ReservoirBufferParameters reservoir;
    RTXDI_DIBufferIndices indices;
    RTXDI_DIInitialSamplingParameters initial;
    RTXDI_DITemporalResamplingParameters temporal;
    RTXDI_DISpatialResamplingParameters spatial;
    RTXDI_LightBufferParameters lights;
    RTXDI_RISBufferSegmentParameters localRis;
    RTXDI_RISBufferSegmentParameters envRis;
    RTXDI_BoilingFilterParameters boiling;
    float4x4 prevViewProj;
    float4x4 prevInvViewProj;
    float4 prevCamera;
    uint historyValid;
    uint readSlice;
    uint writeSlice;
    uint pdfWidth;
    uint pdfHeight;
    uint tlasLo;
    uint tlasHi;
    uint reservoirBase;
    uint lightCount;
    float sunDisk;
    uint pad1;
    uint pad2;
    float shadowRange;
    float shadowBias;
    uint shadowRays;
    uint pad3;
};

struct RestirPush {
    uint pass;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct RAB_Material {
    float3 diffuseAlbedo;
    float3 specularF0;
    float roughness;
};

struct RAB_Surface {
    float3 worldPos;
    float3 viewDir;
    float viewDepth;
    float3 normal;
    float3 geoNormal;
    float diffuseProbability;
    float NdotV;
    RAB_Material material;
};

struct RAB_LightInfo {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

struct RAB_LightSample {
    float3 position;
    float3 normal;
    float3 radiance;
    float solidAnglePdf;
};

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(11, 0)]] RWStructuredBuffer<RestirSlot> gReservoir;
[[vk::binding(12, 0)]] StructuredBuffer<float2> gNeighbor;
[[vk::binding(14, 0)]] RWStructuredBuffer<uint2> gRis;
[[vk::binding(15, 0)]] ConstantBuffer<RestirConstants> gRestir;
[[vk::binding(16, 0)]] RWTexture2D<float4> gHistPos;
[[vk::binding(17, 0)]] RWTexture2D<uint4> gHistMat;
[[vk::binding(18, 0)]] Texture2D gPdf;
[[vk::binding(19, 0)]] StructuredBuffer<RAB_LightInfo> gLights;
[[vk::push_constant]] RestirPush gPush;

#define RTXDI_LIGHT_RESERVOIR_BUFFER gReservoir
#define RTXDI_NEIGHBOR_OFFSETS_BUFFER gNeighbor
#define RTXDI_RIS_BUFFER gRis

static const float kRestirBackground = -1.0;

uint restirBuf(uint index) {
    return index + gRestir.reservoirBase;
}

uint2 restirSliceOrigin(uint slice) {
    return uint2((slice & 1u) * gFrame.extent.x, (slice >> 1u) * gFrame.extent.y);
}

float restirLuma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

uint restirPackUnorm16(float x) {
    return uint(saturate(x) * 65535.0 + 0.5);
}

float restirUnpackUnorm16(uint x) {
    return float(x) / 65535.0;
}

uint restirPackOct(float3 n) {
    float2 e = octEncode(n);
    return restirPackUnorm16(e.x) | (restirPackUnorm16(e.y) << 16);
}

float3 restirUnpackOct(uint packed) {
    return octDecode(float2(restirUnpackUnorm16(packed & 0xffffu), restirUnpackUnorm16(packed >> 16)));
}

uint restirPackUnorm8(float x) {
    return uint(saturate(x) * 255.0 + 0.5);
}

uint restirPackRgb(float3 c) {
    return restirPackUnorm8(c.x) | (restirPackUnorm8(c.y) << 8) | (restirPackUnorm8(c.z) << 16);
}

float3 restirUnpackRgb(uint packed) {
    return float3(float(packed & 0xffu), float((packed >> 8) & 0xffu), float((packed >> 16) & 0xffu)) / 255.0;
}

RAB_LightInfo RAB_EmptyLightInfo() {
    RAB_LightInfo info = (RAB_LightInfo)0;
    return info;
}

RAB_LightSample RAB_EmptyLightSample() {
    RAB_LightSample sample = (RAB_LightSample)0;
    return sample;
}

RAB_Surface RAB_EmptySurface() {
    RAB_Surface surface = (RAB_Surface)0;
    surface.viewDepth = kRestirBackground;
    return surface;
}

bool RAB_IsSurfaceValid(RAB_Surface surface) {
    return surface.viewDepth >= 0.0;
}

float3 RAB_GetSurfaceWorldPos(RAB_Surface surface) {
    return surface.worldPos;
}

RAB_Material RAB_GetMaterial(RAB_Surface surface) {
    return surface.material;
}

float3 RAB_GetSurfaceNormal(RAB_Surface surface) {
    return surface.normal;
}

float RAB_GetSurfaceLinearDepth(RAB_Surface surface) {
    return surface.viewDepth;
}

int RAB_TranslateLightIndex(uint lightIndex, bool currentToPrevious) {
    (void)currentToPrevious;
    return int(lightIndex);
}

RAB_LightInfo RAB_LoadLightInfo(uint index, bool prevFrame) {
    (void)prevFrame;
    if (index >= gRestir.lightCount) {
        return RAB_EmptyLightInfo();
    }
    return gLights[index];
}

RAB_LightInfo RAB_LoadCompactLightInfo(uint linearIndex) {
    (void)linearIndex;
    return RAB_EmptyLightInfo();
}

bool RAB_StoreCompactLightInfo(uint linearIndex, RAB_LightInfo lightInfo) {
    (void)linearIndex;
    (void)lightInfo;
    return false;
}

float RAB_GetLightTargetPdfForVolume(RAB_LightInfo light, float3 volumeCenter, float volumeRadius) {
    (void)light;
    (void)volumeCenter;
    (void)volumeRadius;
    return 0.0;
}

float RAB_EvaluateLocalLightSourcePdf(uint lightIndex) {
    (void)lightIndex;
    return 1.0 / max(float(gRestir.lights.localLightBufferRegion.numLights), 1.0);
}

float RAB_EvaluateEnvironmentMapSamplingPdf(float3 L) {
    (void)L;
    return 0.0;
}

float2 RAB_GetEnvironmentMapRandXYFromDir(float3 worldDir) {
    (void)worldDir;
    return float2(0.0, 0.0);
}

bool RAB_IsAnalyticLightSample(RAB_LightSample lightSample) {
    (void)lightSample;
    return true;
}

float RAB_LightSampleSolidAnglePdf(RAB_LightSample lightSample) {
    return lightSample.solidAnglePdf;
}

int2 RAB_ClampSamplePositionIntoView(int2 pixelPosition, bool prevFrame) {
    (void)prevFrame;
    int2 last = int2(gFrame.extent) - int2(1, 1);
    return clamp(pixelPosition, int2(0, 0), last);
}

bool RAB_AreMaterialsSimilar(RAB_Material a, RAB_Material b) {
    float3 delta = abs(a.diffuseAlbedo - b.diffuseAlbedo);
    return abs(a.roughness - b.roughness) < 0.25 && (delta.x + delta.y + delta.z) < 0.4;
}

float3 restirShade(RAB_LightSample lightSample, RAB_Surface surface) {
    if (lightSample.solidAnglePdf <= 0.0) {
        return 0.0;
    }
    float3 toL = lightSample.position - surface.worldPos;
    float dist = length(toL);
    if (dist <= 1e-4) {
        return 0.0;
    }
    float3 L = toL / dist;
    if (dot(L, surface.geoNormal) <= 0.0) {
        return 0.0;
    }
    float ndl = saturate(dot(surface.normal, L));
    float3 V = surface.viewDir;
    float rough = max(surface.material.roughness, 0.045);
    float a = max(rough * rough, 0.002);
    float3 H = normalize(V + L);
    float3 F = F_Schlick(saturate(dot(V, H)), surface.material.specularF0);
    float D = D_GGX(saturate(dot(surface.normal, H)), a);
    float G = G_Smith(max(surface.NdotV, 1e-3), ndl, a);
    float3 spec = min((D * G * F) / max(4.0 * max(surface.NdotV, 1e-3) * max(ndl, 1e-3), 1e-4), float3(2.0, 2.0, 2.0));
    float3 diff = surface.material.diffuseAlbedo * hammonDiffuse(ndl, max(surface.NdotV, 1e-3), saturate(dot(V, L)), rough) * (1.0 - F);
    return (diff + spec) * ndl * lightSample.radiance / lightSample.solidAnglePdf;
}

float RAB_GetLightSampleTargetPdfForSurface(RAB_LightSample lightSample, RAB_Surface surface) {
    return restirLuma(restirShade(lightSample, surface));
}

void RAB_GetLightDirDistance(RAB_Surface surface, RAB_LightSample lightSample, out float3 o_lightDir, out float o_lightDistance) {
    float3 toLight = lightSample.position - surface.worldPos;
    o_lightDistance = length(toLight);
    o_lightDir = toLight / max(o_lightDistance, 1e-6);
}

RAB_LightSample RAB_SamplePolymorphicLight(RAB_LightInfo lightInfo, RAB_Surface surface, float2 uv) {
    (void)uv;
    RAB_LightSample sample = RAB_EmptyLightSample();
    uint kind = lightInfo.meta.x;
    if (kind == 0u) {
        return sample;
    }
    if (kind == 1u && (gPass.passMask & 512u) == 0u) {
        return sample;
    }
    if (kind == 2u && (gPass.passMask & 65536u) == 0u) {
        return sample;
    }
    if ((lightInfo.meta.y & 1u) != 0u && (gPass.passMask & 131072u) == 0u) {
        return sample;
    }
    float3 toL = lightInfo.posRadius.xyz - surface.worldPos;
    float dist = length(toL);
    float radius = lightInfo.posRadius.w;
    if (radius <= 0.0 || dist >= radius || dist <= 1e-4) {
        return sample;
    }
    float3 L = toL / dist;
    if (kind == 2u && dot(L, -normalize(lightInfo.dirAngle.xyz)) < cos(max(lightInfo.dirAngle.w, 0.05))) {
        return sample;
    }
    float atten = saturate(1.0 - dist / radius);
    atten *= atten;
    float3 radiance = lightInfo.colorIntensity.rgb * lightInfo.colorIntensity.w;
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    float cap = lerp(1.2, 8.0, saturate(gShade.lamp));
    if (peak > cap) {
        radiance *= cap / peak;
    }
    sample.position = lightInfo.posRadius.xyz;
    sample.normal = -L;
    sample.radiance = radiance * atten;
    sample.solidAnglePdf = 1.0;
    return sample;
}

RaytracingAccelerationStructure restirScene() {
    return RaytracingAccelerationStructure(uint64_t(gRestir.tlasLo) | (uint64_t(gRestir.tlasHi) << 32));
}

bool restirTrace(float3 origin, float3 direction, float tMin, float tMax) {
    if (gRestir.tlasLo == 0u && gRestir.tlasHi == 0u) {
        return false;
    }
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = tMin;
    ray.TMax = max(tMax, tMin);
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(restirScene(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

bool RAB_TraceRayForLocalLight(float3 origin, float3 direction, float tMin, float tMax, out uint o_lightIndex, out float2 o_randXY) {
    o_lightIndex = RTXDI_INVALID_LIGHT_INDEX;
    o_randXY = 0.0;
    return restirTrace(origin, direction, tMin, tMax);
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, RAB_LightSample lightSample) {
    if (gRestir.tlasLo == 0u && gRestir.tlasHi == 0u) {
        return true;
    }
    float3 toL = lightSample.position - surface.worldPos;
    float dist = length(toL);
    if (dist < 1e-3) {
        return false;
    }
    float tMin = max(gRestir.shadowBias, 0.02);
    float reach = max(dist - tMin, tMin);
    if (gRestir.shadowRange > 0.0) {
        reach = min(reach, gRestir.shadowRange);
    }
    float3 origin = surface.worldPos + surface.geoNormal * 0.02;
    return !restirTrace(origin, toL / dist, tMin, reach);
}

bool RAB_GetConservativeVisibility(RAB_Surface surface, float3 samplePosition) {
    (void)surface;
    (void)samplePosition;
    return true;
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface, RAB_LightSample lightSample) {
    (void)prevSurface;
    return RAB_GetConservativeVisibility(curSurface, lightSample);
}

bool RAB_GetTemporalConservativeVisibility(RAB_Surface curSurface, RAB_Surface prevSurface, float3 samplePosition) {
    (void)curSurface;
    (void)prevSurface;
    (void)samplePosition;
    return true;
}

float3 restirTangent(RAB_Surface surface, float3 h) {
    float3 axis = abs(surface.normal.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(axis, surface.normal));
    float3 bitangent = cross(surface.normal, tangent);
    return bitangent * h.x + tangent * h.y + surface.normal * h.z;
}

bool RAB_SurfaceImportanceSampleBrdf(RAB_Surface surface, inout RTXDI_RandomSamplerState rng, out float3 dir) {
    float u = RTXDI_GetNextRandom(rng);
    float v = RTXDI_GetNextRandom(rng);
    float r = sqrt(u);
    float phi = v * 6.2831853;
    dir = restirTangent(surface, float3(r * cos(phi), r * sin(phi), sqrt(max(1.0 - u, 0.0))));
    return dot(surface.normal, dir) > 0.0;
}

float RAB_SurfaceEvaluateBrdfPdf(RAB_Surface surface, float3 dir) {
    return saturate(dot(surface.normal, dir)) / kPi;
}

RAB_Surface restirSurfaceFrom(float3 worldPos, float viewDepth, float3 normal, float3 albedo, float roughness, float metallic) {
    RAB_Surface surface = RAB_EmptySurface();
    if (viewDepth <= 0.0) {
        return surface;
    }
    surface.worldPos = worldPos;
    surface.viewDepth = viewDepth;
    surface.normal = normal;
    surface.geoNormal = normal;
    surface.viewDir = normalize(gFrame.cameraPos.xyz - worldPos);
    surface.NdotV = saturate(dot(normal, surface.viewDir));
    surface.material.diffuseAlbedo = albedo * (1.0 - metallic);
    surface.material.specularF0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    surface.material.roughness = roughness;
    float diffuseWeight = restirLuma(surface.material.diffuseAlbedo);
    float specularWeight = restirLuma(surface.material.specularF0);
    float sum = diffuseWeight + specularWeight;
    surface.diffuseProbability = sum < 1e-5 ? 1.0 : diffuseWeight / sum;
    return surface;
}

RAB_Surface RAB_GetGBufferSurface(int2 pixelPosition, bool prevFrame) {
    if (any(pixelPosition < int2(0, 0)) || any(pixelPosition >= int2(gFrame.extent))) {
        return RAB_EmptySurface();
    }
    uint2 px = uint2(pixelPosition);
    if (prevFrame) {
        if (gRestir.historyValid == 0u) {
            return RAB_EmptySurface();
        }
        uint2 at = restirSliceOrigin(gRestir.readSlice) + px;
        float4 pos = gHistPos[at];
        uint4 mat = gHistMat[at];
        return restirSurfaceFrom(pos.xyz, pos.w, restirUnpackOct(mat.x), restirUnpackRgb(mat.y), float(mat.z & 0xffffu) / 65535.0, float(mat.z >> 16) / 65535.0);
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0001) {
        return RAB_EmptySurface();
    }
    float4 s0 = gSurf0[px];
    float4 s1 = gSurf1[px];
    float3 world = reconstructWorld(px, depth);
    float viewDepth = length(gFrame.cameraPos.xyz - world);
    return restirSurfaceFrom(world, viewDepth, octDecode(s1.xy), s0.rgb, s0.w, s1.z);
}

void restirWriteHistory(uint2 px, RAB_Surface surface, float3 albedo, float roughness, float metallic) {
    uint2 at = restirSliceOrigin(gRestir.writeSlice) + px;
    if (!RAB_IsSurfaceValid(surface)) {
        gHistPos[at] = float4(0.0, 0.0, 0.0, 0.0);
        gHistMat[at] = uint4(0, 0, 0, 0);
        return;
    }
    gHistPos[at] = float4(surface.worldPos, surface.viewDepth);
    uint roughMetal = restirPackUnorm16(roughness) | (restirPackUnorm16(metallic) << 16);
    gHistMat[at] = uint4(restirPackOct(surface.normal), restirPackRgb(albedo), roughMetal, 0u);
}

float3 restirMotion(uint2 px, RAB_Surface surface) {
    float4 prevClip = mul(gRestir.prevViewProj, float4(surface.worldPos, 1.0));
    float2 prevNdc = prevClip.xy / max(prevClip.w, 1e-6);
    float2 prevPx = (prevNdc * 0.5 + 0.5) * float2(gFrame.extent) - 0.5;
    float prevDepth = length(gRestir.prevCamera.xyz - surface.worldPos);
    return float3(prevPx - float2(px), prevDepth - surface.viewDepth);
}

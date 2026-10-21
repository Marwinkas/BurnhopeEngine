#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "core/atmosphere.slang"
#include "sharc_params.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] globallycoherent RWByteAddressBuffer gAcc;
[[vk::binding(72, 0)]] RWByteAddressBuffer gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;
[[vk::binding(77, 0)]] RWByteAddressBuffer gColor;

#include "sharc_cache.slang"

struct QueryPush {
    uint denoise;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
QueryPush gPush;

// Статичный пространственный Interleaved Gradient Noise (БЕЗ frameIndex — никогда не мигает во времени!)
float2 querySpatialDither(uint2 px) {
    float u = frac(52.9829189 * frac(0.06711056 * float(px.x) + 0.00583715 * float(px.y))) - 0.5;
    float v = frac(52.9829189 * frac(0.00583715 * float(px.x) + 0.06711056 * float(px.y))) - 0.5;
    return float2(u, v);
}

void sharcAddHdr(uint2 px, float3 irradiance, float3 albedo, float metal) {
    float3 cleanIrr = max(irradiance, 0.0);
    if (any(isnan(cleanIrr)) || any(isinf(cleanIrr))) {
        cleanIrr = 0.0;
    }
    if (gPush.denoise != 0u) {
        const uint o = (px.y * gFrame.extent.x + px.x) * 16u;
        gColor.Store(o, asuint(cleanIrr.r));
        gColor.Store(o + 4u, asuint(cleanIrr.g));
        gColor.Store(o + 8u, asuint(cleanIrr.b));
        gColor.Store(o + 12u, asuint(1.0));
        return;
    }
    const float gain = max(gSharc.giGain, 0.0);
    float3 add = cleanIrr * max(albedo, float3(0.02, 0.02, 0.02)) * (1.0 - metal) * gain;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(add * 0.8, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += add;
    gHdr[px] = hdr;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcQuery(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    if (gVis.Load(int3(px, 0)) == 0u || (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u)) {
        if (gFrame.debugMode == 4u || gPush.denoise != 0u) {
            sharcAddHdr(px, 0.0, float3(1.0, 1.0, 1.0), 0.0);
        }
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        if (gFrame.debugMode == 4u || gPush.denoise != 0u) {
            sharcAddHdr(px, 0.0, float3(1.0, 1.0, 1.0), 0.0);
        }
        return;
    }
    float4 surf0 = gSurf0[px];
    float4 surf1 = gSurf1[px];
    float3 albedo = max(surf0.rgb, 0.0);
    float metal = saturate(surf1.z);
    float ao = saturate(surf1.w);
    float3 position = reconstructWorld(px, depth);

    float3 geoNormal = reconstructNormalFromDepth(gDepth, px, position);
    float3 surfNormal = octDecode(surf1.xy);
    if (dot(geoNormal, geoNormal) < 0.5) {
        geoNormal = dot(surfNormal, surfNormal) >= 0.5 ? normalize(surfNormal) : float3(0.0, 1.0, 0.0);
    } else {
        geoNormal = normalize(geoNormal);
    }

    float3 view = gFrame.cameraPos.xyz - position;
    if (dot(geoNormal, view) < 0.0) {
        geoNormal = -geoNormal;
    }

    SharcGrid grid = sharcGrid();
    float3 queryPos = position;

    // Статичный пространственный дизер в касательной плоскости (сглаживается фильтрами без покадрового шума)
    if (gPush.denoise != 0u) {
        float voxel = sharcVoxelSize(sharcLevel(position, grid), grid);
        float3 axis = abs(geoNormal.y) < 0.999 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 tangent = normalize(cross(axis, geoNormal));
        float3 bitangent = cross(geoNormal, tangent);
        float2 j = querySpatialDither(px) * (voxel * 0.25);
        queryPos += tangent * j.x + bitangent * j.y;
    }

    SharcHitData sharcHit;
    sharcHit.positionWorld = queryPos;
    sharcHit.normalWorld = geoNormal;
    sharcHit.materialDemodulation = float3(1.0, 1.0, 1.0);

    float3 cached = 0.0;
    if (!sharcGetCachedRadiance(sharcHit, grid, cached) || sharcLuma(max(cached, 0.0)) <= 1e-4) {
        sharcAddHdr(px, 0.0, albedo, metal);
        return;
    }
    sharcAddHdr(px, max(cached, 0.0) * ao, albedo, metal);
}
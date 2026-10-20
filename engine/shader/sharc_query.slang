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

void sharcAddHdr(uint2 px, float3 indirect, float3 albedo) {
    float3 add = max(indirect, 0.0);
    if (!all(add == add)) {
        add = 0.0;
    }
    if (gPush.denoise != 0u) {
        const float3 denom = max(albedo, float3(0.01, 0.01, 0.01));
        const float3 irr = add / denom;
        const uint o = (px.y * gFrame.extent.x + px.x) * 16u;
        gColor.Store(o, asuint(irr.r));
        gColor.Store(o + 4u, asuint(irr.g));
        gColor.Store(o + 8u, asuint(irr.b));
        gColor.Store(o + 12u, asuint(1.0));
        return;
    }
    const float gain = max(gSharc.giGain, 0.0);
    add *= gain;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(add.r * 0.8, add.g * 0.8, add.b * 0.8, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.r = hdr.r + add.r;
    hdr.g = hdr.g + add.g;
    hdr.b = hdr.b + add.b;
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
            sharcAddHdr(px, 0.0, float3(1.0, 1.0, 1.0));
        }
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        if (gFrame.debugMode == 4u || gPush.denoise != 0u) {
            sharcAddHdr(px, 0.0, float3(1.0, 1.0, 1.0));
        }
        return;
    }
    float4 surf0 = gSurf0[px];
    float4 surf1 = gSurf1[px];
    float3 albedo = max(surf0.rgb, 0.0);
    float metal = saturate(surf1.z);
    float ao = saturate(surf1.w);
    float3 position = reconstructWorld(px, depth);
    float3 normal = octDecode(surf1.xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    } else {
        normal = normalize(normal);
    }
    float3 view = gFrame.cameraPos.xyz - position;
    if (dot(normal, view) < 0.0) {
        normal = -normal;
    }
    SharcHitData sharcHit;
    sharcHit.positionWorld = position;
    sharcHit.normalWorld = normal;
    sharcHit.materialDemodulation = max(albedo, float3(0.05, 0.05, 0.05));
    float3 cached = 0.0;
    if (!sharcGetCachedRadiance(sharcHit, sharcGrid(), cached) || sharcLuma(max(cached, 0.0)) <= 1e-3) {
        sharcAddHdr(px, 0.0, albedo);
        return;
    }
    sharcAddHdr(px, max(cached, 0.0) * ao * (1.0 - metal), albedo);
}

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
#include "sharc_hit.slang"

struct QueryPush {
    uint denoise;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
QueryPush gPush;

void sharcAddHdr(uint2 px, float3 indirect) {
    float3 add = max(indirect, 0.0);
    if (!all(add == add)) {
        add = 0.0;
    }
    if (gPush.denoise != 0u) {
        const uint o = (px.y * gFrame.extent.x + px.x) * 16u;
        gColor.Store(o, asuint(add.r));
        gColor.Store(o + 4u, asuint(add.g));
        gColor.Store(o + 8u, asuint(add.b));
        gColor.Store(o + 12u, asuint(1.0));
        return;
    }
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
            sharcAddHdr(px, 0.0);
        }
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        if (gFrame.debugMode == 4u || gPush.denoise != 0u) {
            sharcAddHdr(px, 0.0);
        }
        return;
    }
    float4 surf0 = gSurf0[px];
    float4 surf1 = gSurf1[px];
    float3 albedo = max(surf0.rgb, 0.0);
    float rough = saturate(surf0.w);
    float metal = saturate(surf1.z);
    float ao = saturate(surf1.w);
    float3 position = reconstructWorld(px, depth);
    float3 normal = octDecode(surf1.xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    } else {
        normal = normalize(normal);
    }
    float3 view = normalize(gFrame.cameraPos.xyz - position);
    if (dot(normal, view) < 0.0) {
        normal = -normal;
    }
    SharcSurf primary = sharcMissSurf();
    primary.position = position;
    primary.geoNormal = normal;
    primary.albedo = albedo;
    primary.roughness = rough;
    primary.metallic = metal;
    primary.hit = true;
    uint rng = px.x * 1973u ^ px.y * 9277u ^ gSharc.frameIndex * 26699u ^ 0x85ebca6bu;
    float3 direction = 0.0;
    float3 throughput = 0.0;
    uint diffuseLobe = 1u;
    if (!sharcSampleBounce(primary, view, rng, direction, throughput, diffuseLobe)) {
        sharcAddHdr(px, 0.0);
        return;
    }
    float roughPrev = diffuseLobe != 0u ? 1.0 : rough;
    float gain = gSharc.giGain > 0.0 ? gSharc.giGain : 1.0;
    throughput *= ao * gain;
    float3 origin = position + normal * 0.02;
    SharcGrid grid = sharcGrid();
    uint bounces = max(gSharc.bouncesMax, 1u);
    float3 indirect = 0.0;
    [loop]
    for (uint bounce = 0u; bounce < bounces; ++bounce) {
        float hitT = 0.0;
        uint prim = 0u;
        float2 bary = 0.0;
        if (!sharcTrace(origin, direction, 0.05, 1000.0, hitT, prim, bary)) {
            break;
        }
        float3 hitP = origin + direction * hitT;
        float3 hitView = -direction;
        SharcSurf hit = sharcSurface(prim, bary, hitP, hitView);
        if (!hit.hit) {
            break;
        }
        if (sharcCacheEligible(hitT, hitP, roughPrev, grid)) {
            SharcHitData sharcHit;
            sharcHit.positionWorld = hitP;
            sharcHit.normalWorld = hit.geoNormal;
            sharcHit.materialDemodulation = hit.demod;
            float3 cached = 0.0;
            if (sharcGetCachedRadiance(sharcHit, grid, cached) && sharcLuma(max(cached, 0.0)) > 1e-3) {
                indirect += max(cached, 0.0) * throughput;
                break;
            }
        }
        float3 direct = sharcDirect(hit, hitView, rng);
        indirect += max(direct, 0.0) * throughput;
        if (bounce + 1u == bounces) {
            break;
        }
        float3 nextDir = 0.0;
        float3 weight = 0.0;
        uint nextDiffuse = 1u;
        if (!sharcSampleBounce(hit, hitView, rng, nextDir, weight, nextDiffuse)) {
            break;
        }
        throughput *= weight;
        roughPrev += nextDiffuse != 0u ? 1.0 : hit.roughness;
        origin = hitP + hit.geoNormal * 0.02;
        direction = nextDir;
    }
    sharcAddHdr(px, indirect);
}

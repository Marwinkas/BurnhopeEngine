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
[[vk::binding(71, 0)]] RWStructuredBuffer<uint4> gAcc;
[[vk::binding(72, 0)]] RWStructuredBuffer<uint4> gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;

#include "sharc_cache.slang"
#include "sharc_hit.slang"

// SHaRC Render: первичный хит уже в G-буфере и в прямой свет не кэшируется.
// На следующем сегменте запрос, если длина больше диагонали вокселя и конус шероховатости шире вокселя.

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcQuery(uint3 dtid : SV_DispatchThreadID) {
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    if (gVis.Load(int3(px, 0)) == 0u) {
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0 || (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u)) {
        return;
    }
    float3 position = reconstructWorld(px, depth);
    float3 normal = octDecode(gSurf1[px].xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    } else {
        normal = normalize(normal);
    }
    float3 view = normalize(gFrame.cameraPos.xyz - position);
    if (dot(normal, view) < 0.0) {
        normal = -normal;
    }
    float3 albedo = max(gSurf0[px].rgb, 0.0);
    float rough = saturate(gSurf0[px].a);
    float metal = saturate(gSurf1[px].z);
    float ao = saturate(gSurf1[px].w);
    SharcSurf surf = sharcMissSurf();
    surf.position = position;
    surf.geoNormal = normal;
    surf.albedo = albedo;
    surf.roughness = rough;
    surf.metallic = metal;
    surf.hit = true;
    float3 diffuse = albedo * (1.0 - metal);
    float3 f0 = computeF0(albedo, metal, computeDielectricF0(0.5));
    float3 fAvg = f0 + (1.0 - f0) * (1.0 / 21.0);
    surf.demod = max(diffuse, float3(0.05, 0.05, 0.05)) + max(f0, float3(0.02, 0.02, 0.02)) * sharcLuma(fAvg);
    uint rng = px.x * 1973u ^ px.y * 9277u ^ gSharc.frameIndex * 26699u ^ 0x85ebca6bu;
    float3 throughput = ao;
    float lobeRough = 0.0;
    float3 indirect = 0.0;
    SharcGrid grid = sharcGrid();
    uint bounces = max(gSharc.bouncesMax, 1u);
    [loop]
    for (uint bounce = 0u; bounce < bounces; ++bounce) {
        float3 nextDir = 0.0;
        float3 weight = 0.0;
        float nextLobe = 1.0;
        if (!sharcSampleBounce(surf, view, rng, nextDir, weight, nextLobe)) {
            break;
        }
        throughput *= weight;
        lobeRough = min(lobeRough + nextLobe, 0.99);
        float hitT = 0.0;
        uint prim = 0u;
        float2 bary = 0.0;
        float3 origin = surf.position + surf.geoNormal * 0.02;
        if (!sharcTrace(origin, nextDir, 0.02, 1000.0, hitT, prim, bary)) {
            indirect += sharcSky(nextDir) * throughput;
            break;
        }
        float3 hitP = origin + nextDir * hitT;
        float3 hitView = -nextDir;
        SharcSurf hit = sharcSurface(prim, bary, hitP, hitView);
        if (!hit.hit) {
            break;
        }
        uint level = sharcLevel(hitP, grid);
        float voxel = sharcVoxelSize(level, grid);
        float alpha = lobeRough * lobeRough;
        float denom = max(1.0 - alpha * alpha, 1e-4);
        float footprint = hitT * sqrt(0.5 * alpha * alpha / denom);
        bool cacheOk = hitT > voxel * sqrt(3.0) && footprint > voxel;
        SharcHitData sharcHit;
        sharcHit.positionWorld = hitP;
        sharcHit.normalWorld = hit.geoNormal;
        sharcHit.materialDemodulation = hit.demod;
        float3 cached = 0.0;
        if (cacheOk && sharcGetCachedRadiance(sharcHit, grid, cached)) {
            indirect += cached * throughput;
            break;
        }
        indirect += sharcDirect(hit, hitView, rng) * throughput;
        if (bounce + 1u == bounces) {
            break;
        }
        surf = hit;
        view = hitView;
    }
    float gain = gSharc.giGain > 1e-4 ? gSharc.giGain : 1.0;
    indirect = max(indirect, 0.0) * gain;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(indirect, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += indirect;
    gHdr[px] = hdr;
}

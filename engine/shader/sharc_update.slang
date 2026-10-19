#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "core/atmosphere.slang"
#include "sharc_params.slang"

[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] RWStructuredBuffer<uint4> gAcc;
[[vk::binding(72, 0)]] RWStructuredBuffer<uint4> gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;

#include "sharc_cache.slang"
#include "sharc_hit.slang"

// Разреженный путь: один пиксель блока downscale×downscale, решётка сдвигается кадром.
// Цикл совпадает с SHaRC Update: сегмент независим, SharcInit / Hit / Miss / SetThroughput.

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcUpdate(uint3 dtid : SV_DispatchThreadID) {
    uint down = max(gSharc.downscale, 1u);
    uint2 coarse = uint2((gFrame.extent.x + down - 1u) / down, (gFrame.extent.y + down - 1u) / down);
    if (dtid.x >= coarse.x || dtid.y >= coarse.y) {
        return;
    }
    if (gSharc.tlasLo == 0u && gSharc.tlasHi == 0u) {
        return;
    }
    uint cell = gSharc.frameIndex % (down * down);
    float2 jitter = float2(float(cell % down), float(cell / down)) + 0.5;
    float2 uv = (float2(dtid.xy) * float(down) + jitter) * gFrame.invExtent;
    float3 origin = gFrame.cameraPos.xyz;
    float3 farP = reconstructWorld(uv, 0.0);
    float3 direction = normalize(farP - origin);
    uint rng = dtid.x * 1973u ^ dtid.y * 9277u ^ gSharc.frameIndex * 26699u ^ 0x9e3779b9u;
    SharcState state;
    sharcInit(state);
    SharcGrid grid = sharcGrid();
    uint bounces = max(gSharc.bouncesMax, 1u);
    [loop]
    for (uint bounce = 0u; bounce < bounces; ++bounce) {
        float hitT = 0.0;
        uint prim = 0u;
        float2 bary = 0.0;
        bool hit = sharcTrace(origin, direction, bounce == 0u ? 0.0 : 0.02, 1000.0, hitT, prim, bary);
        if (!hit) {
            sharcUpdateMiss(state, sharcSky(direction));
            break;
        }
        float3 position = origin + direction * hitT;
        float3 view = -direction;
        SharcSurf surf = sharcSurface(prim, bary, position, view);
        if (!surf.hit) {
            break;
        }
        surf.roughness = max(surf.roughness, gSharc.roughnessThreshold);
        float3 direct = sharcDirect(surf, view, rng);
        SharcHitData sharcHit;
        sharcHit.positionWorld = position;
        sharcHit.normalWorld = surf.geoNormal;
        sharcHit.materialDemodulation = surf.demod;
        bool keep = sharcUpdateHit(state, sharcHit, direct, sharcRand(rng), grid);
        if (bounce + 1u == bounces || !keep) {
            break;
        }
        float3 nextDir = 0.0;
        float3 weight = 0.0;
        float lobe = 1.0;
        if (!sharcSampleBounce(surf, view, rng, nextDir, weight, lobe)) {
            break;
        }
        sharcSetThroughput(state, weight);
        origin = position + surf.geoNormal * 0.02;
        direction = nextDir;
    }
}

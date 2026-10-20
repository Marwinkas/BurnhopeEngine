#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "core/atmosphere.slang"
#include "sharc_params.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(70, 0)]] RWStructuredBuffer<uint64_t> gHash;
[[vk::binding(71, 0)]] globallycoherent RWByteAddressBuffer gAcc;
[[vk::binding(72, 0)]] RWByteAddressBuffer gResolved;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;
[[vk::binding(15, 0)]] RWStructuredBuffer<uint2> gRayStat;

#include "sharc_cache.slang"
#include "sharc_hit.slang"

// Тайл читает глубину. Один непрозрачный луч уходит в полусферу нормали. Энергия пишется в ячейку этой точки.

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcUpdate(uint3 dtid : SV_DispatchThreadID) {
    uint cacheIndex = kSharcInvalid;
    uint4 packed = 0;
    uint down = max(gSharc.downscale, 1u);
    uint2 coarse = uint2((gFrame.extent.x + down - 1u) / down, (gFrame.extent.y + down - 1u) / down);
    bool live = (gSharc.tlasLo != 0u || gSharc.tlasHi != 0u) && dtid.x < coarse.x && dtid.y < coarse.y;
    if (live) {
        uint2 px = dtid.xy * down + uint2(down / 2u, down / 2u);
        px = min(px, gFrame.extent.xy - uint2(1, 1));
        float depth = gDepth.Load(int3(px, 0));
        if (gVis.Load(int3(px, 0)) != 0u && depth > 0.0) {
            float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
            float3 position = reconstructWorld(uv, depth);
            float3 normal = octDecode(gSurf1[px].xy);
            if (dot(normal, normal) < 0.5) {
                normal = reconstructNormalFromDepth(gDepth, px, position);
            }
            normal = normalize(normal);
            float3 view = gFrame.cameraPos.xyz - position;
            view = dot(view, view) > 1e-8 ? normalize(view) : normal;
            if (dot(normal, view) < 0.0) {
                normal = -normal;
            }
            int3 bin = int3(floor(position / 0.5));
            uint rng = uint(bin.x) * 1973u ^ uint(bin.y) * 9277u ^ uint(bin.z) * 26699u;
            float turn = sharcRand(rng) * 6.2831853;
            float cs = cos(turn);
            float sn = sin(turn);
            float3 origin = position + normal * 0.02;
            float inten = gSun.sunDirIntensity.w;
            if (inten < 0.05) {
                inten = 8.0;
            }
            float3 sky = sharcSkyFill() / inten;
            float3 color = 0.0;
            uint hits = 0u;
            [loop]
            for (uint ray = 0u; ray < 4u; ++ray) {
                float2 disk = float2(float(ray & 1u) + 0.5, float(ray >> 1u) + 0.5) * 0.5 - 0.5;
                float2 spun = float2(cs * disk.x - sn * disk.y, sn * disk.x + cs * disk.y) + 0.5;
                float3 direction = sharcCosine(normal, saturate(spun));
                float hitT = 0.0;
                uint prim = 0u;
                float2 bary = 0.0;
                uint ignored;
                InterlockedAdd(gRayStat[1024u * 128u + 9u].x, 1u, ignored);
                if (!sharcTrace(origin, direction, 0.05, 80.0, hitT, prim, bary) || hitT < 0.15) {
                    continue;
                }
                float3 hitP = origin + direction * hitT;
                SharcSurf surf = sharcSurface(prim, bary, hitP, -direction);
                if (!surf.hit) {
                    continue;
                }
                color += max(sharcDirectLite(surf, -direction) - sky, 0.0);
                hits += 1u;
            }
            if (hits > 0u) {
                sharcPaintEnergy(position, normal, color / float(hits) * 2.5, true);
            } else {
                sharcPaintEnergy(position, normal, sky * 0.35, false);
            }
        }
    }
    sharcFlushWave(cacheIndex, packed);
}

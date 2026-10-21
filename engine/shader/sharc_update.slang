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

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcUpdate(uint3 dtid : SV_DispatchThreadID) {
    uint cacheIndex = kSharcInvalid;
    uint4 packed = 0;
    uint down = max(gSharc.downscale, 1u);
    uint2 coarse = uint2((gFrame.extent.x + down - 1u) / down, (gFrame.extent.y + down - 1u) / down);
    bool live = (gSharc.tlasLo != 0u || gSharc.tlasHi != 0u) && dtid.x < coarse.x && dtid.y < coarse.y;
    if (live) {
        // Равномерный обход субпикселей тайла, чтобы ни один участок стены не оставался без лучей
        uint phase = (gSharc.frameIndex + dtid.x * 3u + dtid.y * 5u) & 15u;
        uint2 subOff = uint2((phase & 3u) * 2u + 1u, (phase >> 2u) * 2u + 1u) % down;
        uint2 px = dtid.xy * down + subOff;
        px = min(px, gFrame.extent.xy - uint2(1, 1));

        float depth = gDepth.Load(int3(px, 0));
        if (gVis.Load(int3(px, 0)) != 0u && depth > 0.0) {
            float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
            float3 position = reconstructWorld(uv, depth);

            float3 geoNormal = reconstructNormalFromDepth(gDepth, px, position);
            float3 surfNormal = octDecode(gSurf1[px].xy);
            if (dot(geoNormal, geoNormal) < 0.5) {
                geoNormal = dot(surfNormal, surfNormal) >= 0.5 ? normalize(surfNormal) : float3(0.0, 1.0, 0.0);
            } else {
                geoNormal = normalize(geoNormal);
            }

            float3 view = gFrame.cameraPos.xyz - position;
            view = dot(view, view) > 1e-8 ? normalize(view) : geoNormal;
            if (dot(geoNormal, view) < 0.0) {
                geoNormal = -geoNormal;
            }

            int3 bin = int3(floor(position / 0.5));
            uint rng = uint(bin.x) * 1973u ^ uint(bin.y) * 9277u ^ uint(bin.z) * 26699u;
            float turn = sharcRand(rng) * 6.2831853;
            float cs = cos(turn);
            float sn = sin(turn);

            SharcGrid grid = sharcGrid();
            float3 origin = position + geoNormal * 0.03;
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
                float3 direction = sharcCosine(geoNormal, saturate(spun));
                float hitT = 0.0;
                uint prim = 0u;
                float2 bary = 0.0;
                uint ignored;
                InterlockedAdd(gRayStat[1024u * 128u + 9u].x, 1u, ignored);

                if (!sharcTrace(origin, direction, 0.04, 100.0, hitT, prim, bary)) {
                    color += sky * 0.35;
                    continue;
                }
                float3 hitP = origin + direction * hitT;
                SharcSurf surf = sharcSurface(prim, bary, hitP, -direction);
                if (!surf.hit) {
                    color += sky * 0.35;
                    continue;
                }

                // Без вычитания `- sky`, которое убивало цвет кирпичей в ноль!
                float3 bounceRad = sharcDirectLite(surf, -direction);

                // Подхватываем вторичный отскок из кэша с безопасным коэффициентом 0.35
                SharcHitData secondHit;
                secondHit.positionWorld = hitP;
                secondHit.normalWorld = surf.geoNormal;
                secondHit.materialDemodulation = surf.albedo * (1.0 - surf.metallic);
                float3 multiBounce = 0.0;
                if (sharcGetCachedRadiance(secondHit, grid, multiBounce)) {
                    bounceRad += multiBounce * 0.35;
                }

                color += bounceRad;
                hits += 1u;
            }

            float3 avgEnergy = (color * 0.25) * 1.35;
            sharcPaintEnergy(position, geoNormal, avgEnergy, true);
        }
    }
    sharcFlushWave(cacheIndex, packed);
}
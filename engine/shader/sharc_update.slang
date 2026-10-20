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
            uint rng = uint(abs(position.x) * 32.0) * 1973u ^ uint(abs(position.y) * 32.0) * 9277u ^ uint(abs(position.z) * 32.0) * 26699u;
            float3 direction = sharcCosine(normal, float2(sharcRand(rng), sharcRand(rng)));
            float hitT = 0.0;
            uint prim = 0u;
            float2 bary = 0.0;
            float3 origin = position + normal * 0.02;
            float3 direct = sharcSkyFill();
            float inten = gSun.sunDirIntensity.w;
            if (inten < 0.05) {
                inten = 8.0;
            }
            direct /= inten;
            if (sharcTrace(origin, direction, 0.05, 80.0, hitT, prim, bary) && hitT >= 0.15) {
                float3 hitP = origin + direction * hitT;
                SharcSurf surf = sharcSurface(prim, bary, hitP, -direction);
                if (surf.hit) {
                    direct = sharcDirectLite(surf, -direction);
                }
            }
            sharcPaintEnergy(position, normal, direct);
        }
    }
    sharcFlushWave(cacheIndex, packed);
}

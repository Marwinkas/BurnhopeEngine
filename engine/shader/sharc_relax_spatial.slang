#include "frame.slang"
#include "core/math.slang"
#include "camera.slang"
#include "sharc_params.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;
[[vk::binding(76, 0)]] RWTexture2D<float4> gTemporal;
[[vk::binding(77, 0)]] RWTexture2D<float4> gHistory;

// Перекрёстный 5×5 по нормали и глубине, затем альбедо и XeGTAO только на непрямой свет.

float3 sharcNormal(uint2 px, float3 position) {
    float3 normal = octDecode(gSurf1[px].xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    }
    return normalize(normal);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcRelaxSpatial(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    if (gVis.Load(int3(px, 0)) == 0u) {
        gHistory[px] = 0.0;
        return;
    }
    float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        gHistory[px] = 0.0;
        return;
    }
    float3 position = reconstructWorld(px, depth);
    float3 normal = sharcNormal(px, position);
    float z0 = max(length(gFrame.cameraPos.xyz - position), 0.05);
    float4 center = gTemporal[px];
    float3 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy) {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx) {
            int2 q = int2(px) + int2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(gFrame.extent.x) || q.y >= int(gFrame.extent.y)) {
                continue;
            }
            float4 tap = gTemporal[uint2(q)];
            if (tap.a < 0.5) {
                continue;
            }
            float qDepth = gDepth.Load(int3(uint2(q), 0));
            if (qDepth <= 0.0) {
                continue;
            }
            float3 qPos = reconstructWorld(uint2(q), qDepth);
            float3 qN = sharcNormal(uint2(q), qPos);
            float nd = saturate(dot(normal, qN));
            float wn = pow(nd, 4.0);
            float z1 = max(length(gFrame.cameraPos.xyz - qPos), 0.05);
            float dz = abs(z0 - z1) / (z0 * 0.15);
            float wz = exp(-dz * dz);
            float w = max(wn * wz, 0.0);
            acc += tap.rgb * w;
            wsum += w;
        }
    }
    float3 filtered = wsum > 1e-4 ? acc / wsum : center.rgb;
    filtered = max(filtered, 0.0);
    float valid = wsum > 1e-4 || center.a > 0.5 ? 1.0 : 0.0;
    gHistory[px] = float4(filtered, valid);
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    float4 surf = gSurf0[px];
    float3 albedo = float3(max(surf.r, 0.0), max(surf.g, 0.0), max(surf.b, 0.0));
    float metal = saturate(gSurf1[px].z);
    float ao = saturate(gSurf1[px].w);
    float gain = gSharc.giGain > 1e-4 ? gSharc.giGain : 1.0;
    float3 indirect = float3(filtered.r * albedo.r, filtered.g * albedo.g, filtered.b * albedo.b);
    indirect *= (1.0 - metal) * ao * gain;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(filtered * 0.8, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += indirect;
    gHdr[px] = hdr;
}

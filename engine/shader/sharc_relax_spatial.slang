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

struct SharcRelaxPush {
    uint step;
    uint flags;
    uint pad0;
    uint pad1;
};

[[vk::push_constant]]
SharcRelaxPush gPush;

[[vk::binding(76, 0)]] RWTexture2D<float4> gTemporal;
[[vk::binding(77, 0)]] RWTexture2D<float4> gHistory;

// RELAX Atrous, пять шагов 1, 2, 4, 8, 16. flags bit0 — направление пинг-понга, bit1 — сборка.
// Пустой центр заполняется соседями по нормали и глубине. Яркость режет только живой центр,
// и ширина реза падает с шагом, как phiLuminance в RELAX.

float3 sharcNormal(uint2 px, float3 position) {
    float3 normal = octDecode(gSurf1[px].xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    }
    return normalize(normal);
}

float relaxLuma(float3 color) {
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float4 relaxLoad(uint2 px) {
    return (gPush.flags & 1u) != 0u ? gHistory[px] : gTemporal[px];
}

void relaxStore(uint2 px, float4 value) {
    if ((gPush.flags & 1u) != 0u) {
        gTemporal[px] = value;
    } else {
        gHistory[px] = value;
    }
}

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcRelaxSpatial(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    bool composite = (gPush.flags & 2u) != 0u;
    if (!composite) {
        if (gVis.Load(int3(px, 0)) == 0u) {
            relaxStore(px, 0.0);
            return;
        }
        float depth = gDepth.Load(int3(px, 0));
        if (depth <= 0.0) {
            relaxStore(px, 0.0);
            return;
        }
        float3 position = reconstructWorld(px, depth);
        float3 normal = sharcNormal(px, position);
        float z0 = max(length(gFrame.cameraPos.xyz - position), 0.05);
        float4 center = relaxLoad(px);
        bool centerLive = center.a >= 1.0;
        int step = int(max(gPush.step, 1u));
        float phi = 0.35 + 1.5 / float(step);
        float3 acc = 0.0;
        float wsum = 0.0;
        float centerL = relaxLuma(max(center.rgb, 0.0));
        [unroll]
        for (int dy = -2; dy <= 2; ++dy) {
            [unroll]
            for (int dx = -2; dx <= 2; ++dx) {
                int2 q = int2(px) + int2(dx, dy) * step;
                if (q.x < 0 || q.y < 0 || q.x >= int(gFrame.extent.x) || q.y >= int(gFrame.extent.y)) {
                    continue;
                }
                float4 tap = relaxLoad(uint2(q));
                if (tap.a < 1.0) {
                    continue;
                }
                float qDepth = gDepth.Load(int3(uint2(q), 0));
                if (qDepth <= 0.0) {
                    continue;
                }
                float3 qPos = reconstructWorld(uint2(q), qDepth);
                float3 qN = sharcNormal(uint2(q), qPos);
                float nd = saturate(dot(normal, qN));
                float wn = pow(nd, 8.0);
                float z1 = max(length(gFrame.cameraPos.xyz - qPos), 0.05);
                float dz = abs(z0 - z1) / (z0 * 0.08);
                float wz = exp(-dz * dz);
                float wl = 1.0;
                if (centerLive) {
                    float tapL = relaxLuma(max(tap.rgb, 0.0));
                    wl = exp(-abs(centerL - tapL) / max(phi * max(centerL, 0.05), 0.02));
                }
                float w = max(wn * wz * wl, 0.0);
                acc += max(tap.rgb, 0.0) * w;
                wsum += w;
            }
        }
        float3 filtered = wsum > 1e-4 ? acc / wsum : max(center.rgb, 0.0);
        float valid = centerLive ? center.a : (wsum > 1e-4 ? 1.0 : 0.0);
        relaxStore(px, float4(max(filtered, 0.0), valid));
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    float4 filteredTex = gHistory[px];
    float3 filtered = max(filteredTex.rgb, 0.0);
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(filtered * 0.8, 1.0);
        return;
    }
    float4 surf = gSurf0[px];
    float3 albedo = float3(max(surf.r, 0.0), max(surf.g, 0.0), max(surf.b, 0.0));
    float metal = saturate(gSurf1[px].z);
    float ao = saturate(gSurf1[px].w);
    float gain = gSharc.giGain > 1e-4 ? gSharc.giGain : 1.0;
    float3 indirect = float3(filtered.r * albedo.r, filtered.g * albedo.g, filtered.b * albedo.b);
    indirect *= (1.0 - metal) * ao * gain;
    float4 hdr = gHdr[px];
    hdr.r = hdr.r + indirect.r;
    hdr.g = hdr.g + indirect.g;
    hdr.b = hdr.b + indirect.b;
    gHdr[px] = hdr;
}

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "rc_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;

#include "rc_surface.slang"

float3 rcIrrTexel(uint probe, uint x, uint y) {
    const uint off = sliceBase() + rcIrrRel() + probe * rcIrrStride() + (y * 6u + x) * 16u;
    return float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
}

// uv октаэдра [0,1] попадает на грань между рамкой и внутренними 4×4.
float3 rcSampleIrr(uint probe, float3 N) {
    const float2 octTexel = octEncode(N) * 4.0 + 1.0;
    const float2 p = clamp(octTexel - 0.5, 0.0, 4.999);
    const uint2 i = uint2(p);
    const float2 f = frac(p);
    const uint2 i1 = min(i + 1u, uint2(5u, 5u));
    const float3 a = rcIrrTexel(probe, i.x, i.y);
    const float3 b = rcIrrTexel(probe, i1.x, i.y);
    const float3 c = rcIrrTexel(probe, i.x, i1.y);
    const float3 d = rcIrrTexel(probe, i1.x, i1.y);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 rcIrradianceLod(float3 P, float3 N, uint lod) {
    const float sp = max(rcSpacing(0u, lod), 1e-3);
    const float3 g = P / sp - 0.5;
    const int3 i0 = int3(floor(g));
    const float3 f = saturate(frac(g));
    float3 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const int3 cell = i0 + int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        const float wx = (corner & 1u) != 0u ? f.x : 1.0 - f.x;
        const float wy = ((corner >> 1u) & 1u) != 0u ? f.y : 1.0 - f.y;
        const float wz = ((corner >> 2u) & 1u) != 0u ? f.z : 1.0 - f.z;
        const float w = wx * wy * wz;
        if (w <= 1e-5) {
            continue;
        }
        const uint probe = rcLookup(0u, rcPackKey(cell, lod));
        if (probe == kInvalid || rcLoad(rcMetaAddr(0u, probe) + 24u) == 0u) {
            continue;
        }
        const float3 e = rcSampleIrr(probe, N);
        if (e.x != e.x || e.y != e.y || e.z != e.z) {
            continue;
        }
        acc += e * w;
        wsum += w;
    }
    return wsum > 1e-4 ? acc / wsum : 0.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcShade(uint3 dtid : SV_DispatchThreadID) {
    if ((gPass.passMask & 262144u) == 0u) {
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    float depth = 0.0;
    if (!rcPixel(dtid.xy, depth)) {
        return;
    }
    const uint2 px = dtid.xy;
    const float3 P = reconstructWorld(px, depth);
    float3 N = octDecode(gSurf1[px].xy);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    }
    const float3 rel = abs(P - gFrame.cameraPos.xyz);
    const float cheb = max(rel.x, max(rel.y, rel.z));
    const float lodf = log2(max(cheb / rcBaseSpacing(), 1.0));
    const uint lod0 = min(uint(lodf), 5u);
    const uint lod1 = min(lod0 + 1u, 5u);
    const float3 e0 = rcIrradianceLod(P, N, lod0);
    const float3 e1 = lod1 == lod0 ? e0 : rcIrradianceLod(P, N, lod1);
    const float gain = max(gRcParams.intensity, 0.0) * max(gShade.pad1, 0.0);
    const float3 incoming = lerp(e0, e1, frac(lodf));
    // Режим GI: только непрямой свет на белом 0.8. Прямое солнце в картинку не пишется.
    // Трассировка уже прочитала затенённый hdr до этой замены.
    if (gFrame.debugMode == 4u) {
        const float3 gi = incoming * float3(0.8, 0.8, 0.8) * gain;
        gHdr[px] = float4(max(gi, 0.0), 1.0);
        return;
    }
    const float3 albedo = gSurf0[px].rgb;
    const float metal = saturate(gSurf1[px].z);
    const float3 gi = incoming * albedo * (1.0 - metal) * gain;
    float4 hdr = gHdr[px];
    hdr.rgb += max(gi, 0.0);
    gHdr[px] = hdr;
}

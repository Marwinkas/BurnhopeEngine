// SIGMA: скалярная видимость солнца и расстояние до окклюдера.
// Жёсткий свет (промах) и контакт (нулевая полутень) не размываются. Радиус — только диск полутени.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "nrd_reproj.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(26, 0)]] RWTexture2D<float4> gPenumbra;
[[vk::binding(27, 0)]] RWTexture2D<float4> gSunLit;
[[vk::binding(28, 0)]] RWTexture2D<float4> gShadowHist;

struct FlightPush {
    uint pass;
    uint flight;
    uint readSlice;
    uint writeSlice;
};

[[vk::push_constant]]
FlightPush gPush;

static const float2 kSigmaDisk[8] = {
    float2(0.55, 0.10), float2(-0.42, 0.48), float2(-0.18, -0.62), float2(0.72, -0.35),
    float2(-0.85, -0.12), float2(0.28, 0.78), float2(-0.62, 0.55), float2(0.15, -0.88)
};

uint2 sigmaAt(uint slice, uint2 px) {
    return uint2(px.x, px.y + slice * gFrame.extent.y);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return;
    }
    const uint2 cur = sigmaAt(gPush.flight, px);
    const uint2 histAt = sigmaAt(gPush.writeSlice, px);
    if (gPush.pass != 0u) {
        const float4 hist = gShadowHist[histAt];
        if (hist.a < 0.5) {
            return;
        }
        const float vis = saturate(hist.r);
        if (gFrame.debugMode == 18u) {
            float4 hdr = gHdr[px];
            hdr.rgb = saturate(hdr.rgb * vis);
            hdr.a = 1.0;
            gHdr[px] = hdr;
            return;
        }
        if (gFrame.debugMode != 0u) {
            return;
        }
        float4 hdr = gHdr[px];
        hdr.rgb += max(gSunLit[cur].rgb, 0.0) * vis;
        gHdr[px] = hdr;
        return;
    }
    const float4 sample = gPenumbra[cur];
    const float depth = gDepth.Load(int3(px, 0));
    if (sample.a < 0.5 || depth <= 0.0) {
        gShadowHist[histAt] = 0.0;
        return;
    }
    float vis = saturate(sample.r);
    const float pen = max(sample.g, 0.0);
    const float3 P = reconstructWorld(px, depth);
    const float viewZ = max(length(P - gFrame.cameraPos.xyz), 1e-3);
    const float3 N = nrdNormal(gSurf1, gDepth, int2(px));
    const bool sharp = pen <= 1e-4 || pen >= 512.0;
    float radius = 0.0;
    if (!sharp) {
        const float3 side = reconstructWorld(uint2(min(px.x + 1u, gFrame.extent.x - 1u), px.y), depth);
        const float pixel = max(length(side - P), viewZ * gFrame.invExtent.x);
        radius = clamp(pen / max(pixel, 1e-4), 0.0, 4.0);
    }
    float acc = vis;
    float wsum = 1.0;
    if (radius > 0.5) {
        const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
        [unroll]
        for (int i = 0; i < 8; ++i) {
            const int2 tap = clamp(int2(px) + int2(round(kSigmaDisk[i] * radius)), int2(0, 0), ext);
            const float4 nb = gPenumbra[sigmaAt(gPush.flight, uint2(tap))];
            if (nb.a < 0.5) {
                continue;
            }
            const float2 off = float2(tap - int2(px));
            float w = exp(-dot(off, off) / max(radius * radius, 1.0));
            const float z1 = max(length(reconstructWorld(uint2(tap), gDepth.Load(int3(tap, 0))) - gFrame.cameraPos.xyz), 1e-3);
            w *= nrdPlane(N, viewZ, nrdNormal(gSurf1, gDepth, tap), z1);
            w *= 1.0 / (1.0 + max(nb.g, 0.0) / max(pen, 1e-3));
            acc += saturate(nb.r) * w;
            wsum += w;
        }
    }
    vis = acc / max(wsum, 1e-4);
    gShadowHist[histAt] = float4(saturate(vis), pen, 1.0, 1.0);
}

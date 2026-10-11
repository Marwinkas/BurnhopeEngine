// SMAA T2x: ребро по яркости, смешение вдоль ребра, второй кадр — история в hdrB.
// Чётность кадра сдвигает выборку на полтекселя.

#include "frame.slang"
#include "core/math.slang"

[[vk::binding(3, 0)]] RWTexture2D<float4> gDst;
[[vk::binding(4, 0)]] RWTexture2D<float4> gHist;
[[vk::binding(6, 0)]] Texture2D<float4> gSrc;

float4 loadPx(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return gSrc.Load(int3(clamp(p, int2(0, 0), e), 0));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void smaaCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const int2 p = int2(dtid.xy);
    const float4 src = loadPx(p);
    if ((gPass.passMask & 4u) == 0u) {
        return;
    }
    const int2 jitter = (gFrame.debugAlign0 & 1u) == 0u ? int2(1, 0) : int2(0, 1);
    const float l = luma(src.rgb);
    const float threshold = max(0.02, l * 0.05);
    float3 color = src.rgb;
    const float lAcross = luma(loadPx(p + jitter).rgb);
    if (abs(l - lAcross) > threshold) {
        const int2 along = int2(jitter.y, jitter.x);
        int span = 1;
        [loop]
        for (int i = 1; i <= 8; ++i) {
            const float a = luma(loadPx(p + along * i).rgb);
            const float b = luma(loadPx(p + jitter + along * i).rgb);
            if (abs(a - b) <= threshold) {
                break;
            }
            ++span;
        }
        const float w = min(0.5, 0.08 * float(span));
        color = lerp(color, loadPx(p + jitter).rgb, w);
    }
    const float4 hist = gHist[p];
    const float3 outc = hist.a > 0.5 ? lerp(color, hist.rgb, 0.5) : color;
    gHist[p] = float4(color, 1.0);
    gDst[p] = float4(outc, src.a);
}

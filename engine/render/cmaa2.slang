// CMAA2: trace a color edge and blend only the pixels that sit on it.
// A single-pixel speck is left alone. The blend never exceeds half a texel.

#include "frame.slang"
#include "core/math.slang"

[[vk::binding(6, 0)]] Texture2D<float4> gSrc;
[[vk::binding(3, 0)]] RWTexture2D<float4> gDst;

float4 loadPx(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return gSrc.Load(int3(clamp(p, int2(0, 0), e), 0));
}

int edgeSpan(int2 p, int2 step, int2 across, float threshold) {
    int n = 0;
    [loop]
    for (int i = 1; i <= 12; ++i) {
        const float a = luma(loadPx(p + step * i).rgb);
        const float b = luma(loadPx(p + across + step * i).rgb);
        if (abs(a - b) <= threshold) {
            break;
        }
        ++n;
    }
    return n;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void cmaa2Cs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const int2 p = int2(dtid.xy);
    const float4 src = loadPx(p);
    if ((gPass.passMask & 8u) == 0u) {
        gDst[p] = src;
        return;
    }
    const float l = luma(src.rgb);
    const float threshold = max(0.01, l * 0.04);
    float3 color = src.rgb;
    float weight = 0.0;

    const float lRight = luma(loadPx(p + int2(1, 0)).rgb);
    if (abs(l - lRight) > threshold) {
        const int len = 1 + edgeSpan(p, int2(0, 1), int2(1, 0), threshold) + edgeSpan(p, int2(0, -1), int2(1, 0), threshold);
        if (len >= 2) {
            const float w = min(0.62, 0.16 * float(len));
            color = lerp(color, loadPx(p + int2(1, 0)).rgb, w);
            weight = w;
        }
    }
    const float lDown = luma(loadPx(p + int2(0, 1)).rgb);
    if (abs(l - lDown) > threshold) {
        const int len = 1 + edgeSpan(p, int2(1, 0), int2(0, 1), threshold) + edgeSpan(p, int2(-1, 0), int2(0, 1), threshold);
        if (len >= 2) {
            const float w = min(0.62, 0.16 * float(len));
            if (w > weight) {
                color = lerp(src.rgb, loadPx(p + int2(0, 1)).rgb, w);
            }
        }
    }
    gDst[p] = float4(color, src.a);
}

#include "frame.slang"
#include "core/math.slang"
#include "camera.slang"

[[vk::binding(1, 1)]] SamplerState gLinear;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(6, 0)]] Texture2D<float4> gColor;
[[vk::binding(34, 0)]] Texture2D<float4> gHistory;
[[vk::binding(35, 0)]] RWTexture2D<float4> gOut;
[[vk::binding(60, 0)]] ConstantBuffer<GradeBlock> gBlock;

float lanczos2(float v) {
    const float x2 = v * v;
    float wA = x2 - 4.0;
    float wB = x2 * wA - wA;
    wA *= wA;
    wB *= wA;
    return wB * (1.0 / 64.0);
}

float4 catmull5(float2 uv, float2 size) {
    const float2 samplePos = uv * size;
    const float2 texPos1 = floor(samplePos - 0.5) + 0.5;
    const float2 f = samplePos - texPos1;
    const float2 f2 = f * f;
    const float2 f3 = f2 * f;
    const float2 w0 = f2 - 0.5 * (f3 + f);
    const float2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
    const float2 w3 = 0.5 * (f3 - f2);
    const float2 w2 = 1.0 - w0 - w1 - w3;
    const float2 w12 = w1 + w2;
    const float2 inv = 1.0 / size;
    const float2 texPos0 = (texPos1 - 1.0) * inv;
    const float2 texPos3 = (texPos1 + 2.0) * inv;
    const float2 texPos12 = (texPos1 + w2 / max(w12, float2(1e-4, 1e-4))) * inv;
    const float k0 = w12.x * w0.y;
    const float k1 = w0.x * w12.y;
    const float k2 = w12.x * w12.y;
    const float k3 = w3.x * w12.y;
    const float k4 = w12.x * w3.y;
    float4 s0 = gHistory.SampleLevel(gLinear, float2(texPos12.x, texPos0.y), 0.0);
    float4 s1 = gHistory.SampleLevel(gLinear, float2(texPos0.x, texPos12.y), 0.0);
    float4 s2 = gHistory.SampleLevel(gLinear, float2(texPos12.x, texPos12.y), 0.0);
    float4 s3 = gHistory.SampleLevel(gLinear, float2(texPos3.x, texPos12.y), 0.0);
    float4 s4 = gHistory.SampleLevel(gLinear, float2(texPos12.x, texPos3.y), 0.0);
    float4 result = k0 * s0 + k1 * s1 + k2 * s2 + k3 * s3 + k4 * s4;
    result *= 1.0 / max(k0 + k1 + k2 + k3 + k4, 1e-4);
    const float4 boxMin = min(s0, min(min(s1, s2), min(s3, s4)));
    const float4 boxMax = max(s0, max(max(s1, s2), max(s3, s4)));
    return clamp(result, boxMin, boxMax);
}

float4 clipBox(float3 boxMin, float3 boxMax, float4 current, float4 history) {
    const bool outside = history.r < boxMin.r || history.g < boxMin.g || history.b < boxMin.b
        || history.r > boxMax.r || history.g > boxMax.g || history.b > boxMax.b;
    if (!outside) {
        return history;
    }
    const float3 r = current.rgb - history.rgb;
    const float3 ir = 1.0 / (0.0001 + r);
    const float3 rmax = (boxMax - history.rgb) * ir;
    const float3 rmin = (boxMin - history.rgb) * ir;
    const float3 imin = min(rmax, rmin);
    const float t = saturate(max(imin.r, max(imin.g, imin.b)));
    return history + (current - history) * t;
}

float3 taaTonemap(float3 c) {
    return c / (1.0 + max(max(c.r, c.g), c.b));
}

float3 taaUntonemap(float3 c) {
    const float peak = max(max(c.r, c.g), c.b);
    return c / max(1.0 - peak, 1.0 / 65504.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void taaCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    const float2 size = float2(gFrame.extent.xy);
    const float2 invPx = gFrame.invExtent;
    const float2 uv = (float2(px) + 0.5) * invPx;
    const float depth = gDepth.Load(int3(px, 0));
    const float3 world = reconstructWorld(px, depth);
    const float4x4 prevVp = float4x4(
        float4(gBlock.prev0.x, gBlock.prev1.x, gBlock.prev2.x, gBlock.prev3.x),
        float4(gBlock.prev0.y, gBlock.prev1.y, gBlock.prev2.y, gBlock.prev3.y),
        float4(gBlock.prev0.z, gBlock.prev1.z, gBlock.prev2.z, gBlock.prev3.z),
        float4(gBlock.prev0.w, gBlock.prev1.w, gBlock.prev2.w, gBlock.prev3.w));
    const float4 prevClip = mul(prevVp, float4(world, 1.0));
    float2 historyUv = prevClip.xy / max(prevClip.w, 1e-5) * 0.5 + 0.5;
    const bool inHistory = historyUv.x > 0.0 && historyUv.y > 0.0 && historyUv.x < 1.0 && historyUv.y < 1.0;
    const float2 jitter = float2(gTone.f6, gTone.f7);
    const float2 reconstructed = uv * size;
    const float2 closest = floor(reconstructed - jitter) + 0.5;
    const float2 p = closest * invPx;
    const float2 dcenter = (closest + jitter) - reconstructed;
    float3 s0 = gColor.SampleLevel(gLinear, p, 0.0).rgb;
    float3 s1 = gColor.SampleLevel(gLinear, p + float2(0.0, -invPx.y), 0.0).rgb;
    float3 s2 = gColor.SampleLevel(gLinear, p + float2(-invPx.x, 0.0), 0.0).rgb;
    float3 s3 = gColor.SampleLevel(gLinear, p + float2(invPx.x, 0.0), 0.0).rgb;
    float3 s4 = gColor.SampleLevel(gLinear, p + float2(0.0, invPx.y), 0.0).rgb;
    float3 s5 = gColor.SampleLevel(gLinear, p + float2(-invPx.x, -invPx.y), 0.0).rgb;
    float3 s6 = gColor.SampleLevel(gLinear, p + float2(invPx.x, -invPx.y), 0.0).rgb;
    float3 s7 = gColor.SampleLevel(gLinear, p + float2(-invPx.x, invPx.y), 0.0).rgb;
    float3 s8 = gColor.SampleLevel(gLinear, p + float2(invPx.x, invPx.y), 0.0).rgb;
    const float3 box5min = min(s0, min(min(s1, s2), min(s3, s4)));
    const float3 box5max = max(s0, max(max(s1, s2), max(s3, s4)));
    const float3 box9min = min(box5min, min(min(s5, s6), min(s7, s8)));
    const float3 box9max = max(box5max, max(max(s5, s6), max(s7, s8)));
    const float w0 = lanczos2(dcenter.x - 1.0);
    const float w1 = lanczos2(dcenter.x);
    const float w2 = lanczos2(dcenter.x + 1.0);
    const float w3 = lanczos2(dcenter.y - 1.0);
    const float w4 = lanczos2(dcenter.y);
    const float w5 = lanczos2(dcenter.y + 1.0);
    const float sum = max((w0 + w1 + w2) * (w3 + w4 + w5), 1e-4);
    float3 filtered = ((s5 * w0 + s1 * w1 + s6 * w2) * w3
        + (s2 * w0 + s0 * w1 + s3 * w2) * w4
        + (s7 * w0 + s4 * w1 + s8 * w2) * w5) / sum;
    filtered = clamp(filtered, box9min, box9max);
    float4 history = inHistory ? catmull5(historyUv, size) : float4(filtered, 1.0);
    float3 m0 = s0 + s1 + s2 + s3 + s4;
    float3 m1 = s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3 + s4 * s4;
    const float3 mean = m0 * 0.2;
    const float3 dev = sqrt(abs(m1 * 0.2 - mean * mean));
    const float3 boxMin = max(lerp(box5min, box9min, 0.5), mean - dev);
    const float3 boxMax = min(lerp(box5max, box9max, 0.5), mean + dev);
    history = clipBox(boxMin, boxMax, float4(filtered, 1.0), history);
    const float alpha = saturate(gTone.f8);
    const float3 mixed = taaUntonemap(lerp(taaTonemap(max(history.rgb, 0.0)), taaTonemap(filtered), alpha));
    gOut[px] = float4(max(mixed, 0.0), 1.0);
}

[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;

[shader("compute")]
[numthreads(8, 8, 1)]
void taaCopy(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    gHdr[dtid.xy] = gOut[dtid.xy];
}

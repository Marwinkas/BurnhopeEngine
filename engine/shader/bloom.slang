#include "frame.slang"
#include "core/math.slang"
#include "core/hiz_atlas.slang"

[[vk::binding(6, 0)]] Texture2D<float4> gHdr;
[[vk::binding(33, 0)]] RWTexture2D<float4> gBloom;

#ifndef BLOOM_LEVEL
#define BLOOM_LEVEL 0
#endif

uint bandY(uint level) {
    return atlasMipY(level, max(gFrame.extent.y >> 1, 1u));
}

uint bandW(uint level) { return max(gFrame.extent.x >> (level + 1u), 1u); }
uint bandH(uint level) { return max(gFrame.extent.y >> (level + 1u), 1u); }

float3 loadBand(uint level, int2 p) {
    const int2 e = int2(int(bandW(level)), int(bandH(level))) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), max(e, int2(0, 0)));
    return gBloom.Load(int2(c.x, int(bandY(level)) + c.y)).rgb;
}

float3 crushHighlight(float3 c) {
    c = max(c - gTone.threshold, 0.0);
    const float f = max(c.r, max(c.g, c.b));
    return c / (1.0 + f * 0.1);
}

float3 box4(float3 a, float3 b, float3 c, float3 d) {
    return (a + b + c + d) * 0.25;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void bloomDown(uint3 dtid : SV_DispatchThreadID) {
    const uint level = BLOOM_LEVEL;
    if (dtid.x >= bandW(level) || dtid.y >= bandH(level)) {
        return;
    }
    float3 c;
#if BLOOM_LEVEL == 0
    const int2 base = int2(dtid.xy) * 2;
    const int2 ext = int2(gFrame.extent.xy) - 1;
    const float3 c0 = gHdr.Load(int3(clamp(base, 0, ext), 0)).rgb;
    const float3 c1 = gHdr.Load(int3(clamp(base + int2(1, 0), 0, ext), 0)).rgb;
    const float3 c2 = gHdr.Load(int3(clamp(base + int2(0, 1), 0, ext), 0)).rgb;
    const float3 c3 = gHdr.Load(int3(clamp(base + int2(1, 1), 0, ext), 0)).rgb;
    float w0 = 1.0 / (1.0 + max(c0.r, max(c0.g, c0.b)));
    float w1 = 1.0 / (1.0 + max(c1.r, max(c1.g, c1.b)));
    float w2 = 1.0 / (1.0 + max(c2.r, max(c2.g, c2.b)));
    float w3 = 1.0 / (1.0 + max(c3.r, max(c3.g, c3.b)));
    const float w = 1.0 / max(w0 + w1 + w2 + w3, 1e-4);
    c = crushHighlight((c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3) * w);
#else
    const int2 p = int2(dtid.xy) * 2;
    const float3 mid = loadBand(level - 1u, p);
    const float3 lt = loadBand(level - 1u, p + int2(-1, -1));
    const float3 rt = loadBand(level - 1u, p + int2(1, -1));
    const float3 rb = loadBand(level - 1u, p + int2(1, 1));
    const float3 lb = loadBand(level - 1u, p + int2(-1, 1));
    const float3 lt2 = loadBand(level - 1u, p + int2(-2, -2));
    const float3 rt2 = loadBand(level - 1u, p + int2(2, -2));
    const float3 rb2 = loadBand(level - 1u, p + int2(2, 2));
    const float3 lb2 = loadBand(level - 1u, p + int2(-2, 2));
    const float3 l = loadBand(level - 1u, p + int2(-2, 0));
    const float3 t = loadBand(level - 1u, p + int2(0, -2));
    const float3 r = loadBand(level - 1u, p + int2(2, 0));
    const float3 b = loadBand(level - 1u, p + int2(0, 2));
    const float3 a0 = box4(lt, rt, rb, lb);
    float3 a1 = box4(mid, l, t, lt2);
    a1 += box4(mid, r, t, rt2);
    a1 += box4(mid, r, b, rb2);
    a1 += box4(mid, l, b, lb2);
    c = a0 * 0.5 + a1 * 0.125;
#endif
    gBloom[int2(int(dtid.x), int(bandY(level) + dtid.y))] = float4(c, 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void bloomUp(uint3 dtid : SV_DispatchThreadID) {
    const uint level = BLOOM_LEVEL;
    if (dtid.x >= bandW(level) || dtid.y >= bandH(level)) {
        return;
    }
    const float2 uv = (float2(dtid.xy) + 0.5) / float2(bandW(level), bandH(level));
    const int2 base = int2(uv * float2(bandW(level + 1u), bandH(level + 1u)));
    float3 c0 = loadBand(level + 1u, base + int2(-1, -1));
    c0 += loadBand(level + 1u, base + int2(1, -1));
    c0 += loadBand(level + 1u, base + int2(1, 1));
    c0 += loadBand(level + 1u, base + int2(-1, 1));
    c0 += 4.0 * loadBand(level + 1u, base);
    float3 c1 = loadBand(level + 1u, base + int2(-1, 0));
    c1 += loadBand(level + 1u, base + int2(0, -1));
    c1 += loadBand(level + 1u, base + int2(1, 0));
    c1 += loadBand(level + 1u, base + int2(0, 1));
    const float3 coarse = (c0 + 2.0 * c1) * (1.0 / 16.0);
    const float3 fine = loadBand(level, int2(dtid.xy));
    gBloom[int2(int(dtid.x), int(bandY(level) + dtid.y))] = float4(fine + coarse, 1.0);
}

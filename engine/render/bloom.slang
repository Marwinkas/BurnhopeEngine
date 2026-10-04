#include "frame.slang"

[[vk::binding(6, 0)]] Texture2D<float4> gHdr;
[[vk::binding(33, 0)]] RWTexture2D<float4> gBloom;

#ifndef BLOOM_LEVEL
#define BLOOM_LEVEL 0
#endif

uint bandY(uint level) {
    uint y = 0;
    [unroll]
    for (uint i = 0; i < 4; ++i) {
        if (i < level) {
            y += max(gFrame.extent.y >> (i + 1u), 1u);
        }
    }
    return y;
}

uint bandW(uint level) { return max(gFrame.extent.x >> (level + 1u), 1u); }
uint bandH(uint level) { return max(gFrame.extent.y >> (level + 1u), 1u); }

float karis(float3 c) {
    return 1.0 / (1.0 + max(dot(c, float3(0.2126, 0.7152, 0.0722)), 1e-4));
}

float3 loadHdr(int2 p) {
    const int2 e = int2(gFrame.extent.xy) - int2(1, 1);
    return max(gHdr.Load(int3(clamp(p, int2(0, 0), e), 0)).rgb - gTone.threshold, 0.0);
}

float3 loadBand(uint level, int2 p) {
    const int2 e = int2(int(bandW(level)), int(bandH(level))) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), max(e, int2(0, 0)));
    return gBloom.Load(int2(c.x, int(bandY(level)) + c.y)).rgb;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void bloomDown(uint3 dtid : SV_DispatchThreadID) {
    const uint level = BLOOM_LEVEL;
    if (dtid.x >= bandW(level) || dtid.y >= bandH(level)) {
        return;
    }
    float3 sum = 0.0;
    float w = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y) {
        [unroll]
        for (int x = 0; x < 2; ++x) {
#if BLOOM_LEVEL == 0
            const float3 s = loadHdr(int2(dtid.xy) * 2 + int2(x, y));
#else
            const float3 s = loadBand(level - 1u, int2(dtid.xy) * 2 + int2(x, y));
#endif
            const float kw = karis(s);
            sum += s * kw;
            w += kw;
        }
    }
    gBloom[int2(int(dtid.x), int(bandY(level) + dtid.y))] = float4(sum / max(w, 1e-4), 1.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void bloomUp(uint3 dtid : SV_DispatchThreadID) {
    const uint level = BLOOM_LEVEL;
    if (dtid.x >= bandW(level) || dtid.y >= bandH(level)) {
        return;
    }
    const float3 fine = loadBand(level, int2(dtid.xy));
    const float2 cuv = (float2(dtid.xy) + 0.5) * 0.5;
    const int2 base = int2(cuv);
    const float2 f = cuv - float2(base);
    float3 coarse = 0.0;
    [unroll]
    for (int y = 0; y <= 1; ++y) {
        [unroll]
        for (int x = 0; x <= 1; ++x) {
            const float wx = x == 0 ? (1.0 - f.x) : f.x;
            const float wy = y == 0 ? (1.0 - f.y) : f.y;
            coarse += loadBand(level + 1u, base + int2(x, y)) * wx * wy;
        }
    }
    gBloom[int2(int(dtid.x), int(bandY(level) + dtid.y))] = float4(fine + coarse, 1.0);
}

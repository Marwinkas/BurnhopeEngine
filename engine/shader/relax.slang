// RELAX: зеркальная яркость и дистанция хита. Виртуальная точка — поверхность плюс отражённый луч на hitDist.
// Зеркало (roughness ≤ 0.06) остаётся резким, история короткая. Шероховатость растягивает a-trous.

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "nrd_reproj.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(36, 0)]] RWTexture2D<float4> gReflect;
[[vk::binding(37, 0)]] RWTexture2D<float4> gHist0;
[[vk::binding(38, 0)]] RWTexture2D<float4> gHist1;

struct FlightPush {
    uint flight;
    uint pass;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
FlightPush gPush;

static const float kRelaxMirror = 0.06;

float4 relaxLoadHist(int2 p) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), ext);
    return gPush.flight == 0u ? gHist1[c] : gHist0[c];
}

void relaxStoreHist(int2 p, float4 v) {
    if (gPush.flight == 0u) {
        gHist0[p] = v;
    } else {
        gHist1[p] = v;
    }
}

float4 relaxLoadOwn(int2 p) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 c = clamp(p, int2(0, 0), ext);
    return gPush.flight == 0u ? gHist0[c] : gHist1[c];
}

float4 relaxSrc(int2 p) {
    if (gPush.pass == 2u) {
        return gReflect[p];
    }
    return relaxLoadOwn(p);
}

void relaxDst(int2 p, float4 v) {
    if (gPush.pass == 2u) {
        relaxStoreHist(p, v);
        return;
    }
    gReflect[p] = v;
}

float3 relaxClamp(int2 ip, float3 hist, float4 srcCenter) {
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    float3 lo = srcCenter.rgb;
    float3 hi = lo;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            const float3 c = max(gReflect[clamp(ip + int2(x, y), int2(0, 0), ext)].rgb, 0.0);
            lo = min(lo, c);
            hi = max(hi, c);
        }
    }
    return clamp(hist, lo, hi);
}

void relaxComposite(uint2 px, float3 resolved, float rough, float hitDist) {
    if (gFrame.debugMode == 7u) {
        gHdr[px] = float4(max(resolved, 0.0), 1.0);
        return;
    }
    if (gFrame.debugMode != 0u || hitDist <= 0.0) {
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 N = nrdNormal(gSurf1, gDepth, int2(px));
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    const float metal = saturate(gSurf1[px].z);
    const float3 f0 = computeF0(gSurf0[px].rgb, metal, 0.04);
    const float2 dfg = dfgEnv(saturate(dot(N, V)), rough);
    float4 hdr = gHdr[px];
    hdr.rgb += max(resolved, 0.0) * (f0 * dfg.x + dfg.y);
    gHdr[px] = hdr;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return;
    }
    const int2 ip = int2(px);
    const float depth = gDepth.Load(int3(ip, 0));
    const float rough = gSurf0[ip].w;
    const float4 raw = gReflect[ip];
    const bool mirror = rough <= kRelaxMirror;
    if (depth <= 0.0 || raw.a <= 0.0) {
        if (gPush.pass == 0u) {
            relaxStoreHist(ip, 0.0);
        } else if (gPush.pass == 3u) {
            relaxDst(ip, 0.0);
        }
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float viewZ = max(length(P - gFrame.cameraPos.xyz), 1e-3);
    const float3 N = nrdNormal(gSurf1, gDepth, ip);
    if (gPush.pass == 0u) {
        float3 signal = max(raw.rgb, 0.0);
        float age = 1.0;
        const float3 V = normalize(gFrame.cameraPos.xyz - P);
        const float3 R = reflect(-V, N);
        const float2 hitUv = nrdPrevUv(P + R * raw.a);
        const float2 surfUv = nrdPrevUv(P);
        const float2 prevUv = nrdInside(hitUv) ? hitUv : surfUv;
        if (nrdInside(prevUv)) {
            const int2 prev = nrdUvPx(prevUv);
            const float4 old = relaxLoadHist(prev);
            const float z1 = max(length(reconstructWorld(uint2(prev), gDepth.Load(int3(prev, 0))) - gFrame.cameraPos.xyz), 1e-3);
            if (old.a >= 1.0 && nrdPlane(N, viewZ, nrdNormal(gSurf1, gDepth, prev), z1) > 0.0) {
                const float cap = mirror ? 8.0 : 32.0;
                const float3 clipped = relaxClamp(ip, max(old.rgb, 0.0), raw);
                age = min(old.a, cap);
                signal = lerp(clipped, signal, 1.0 / (age + 1.0));
                age = min(age + 1.0, cap);
            }
        }
        relaxStoreHist(ip, float4(max(signal, 0.0), age));
        return;
    }
    const float base = gPush.pass == 1u ? 1.0 : (gPush.pass == 2u ? 2.0 : 4.0);
    const float radius = mirror ? 0.0 : base * saturate(rough * 2.0);
    const float4 center = relaxSrc(ip);
    float3 acc = max(center.rgb, 0.0);
    float wsum = 1.0;
    if (radius > 0.25) {
        const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
        const int2 taps[8] = {
            int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1),
            int2(1, 1), int2(1, -1), int2(-1, 1), int2(-1, -1)
        };
        [unroll]
        for (int i = 0; i < 8; ++i) {
            const int2 tap = clamp(ip + int2(round(float2(taps[i]) * radius)), int2(0, 0), ext);
            const float4 nb = relaxSrc(tap);
            if (nb.a < 1.0) {
                continue;
            }
            const float2 off = float2(tap - ip);
            float w = exp(-dot(off, off) / max(radius * radius, 1.0));
            const float z1 = max(length(reconstructWorld(uint2(tap), gDepth.Load(int3(tap, 0))) - gFrame.cameraPos.xyz), 1e-3);
            w *= nrdPlane(N, viewZ, nrdNormal(gSurf1, gDepth, tap), z1);
            acc += max(nb.rgb, 0.0) * w;
            wsum += w;
        }
    }
    const float3 blurred = acc / max(wsum, 1e-4);
    const float4 outv = float4(blurred, max(center.a, 1.0));
    relaxDst(ip, outv);
    if (gPush.pass == 3u) {
        relaxComposite(px, blurred, rough, raw.a);
    }
}

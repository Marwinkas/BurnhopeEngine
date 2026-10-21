// SVGF / ReBLUR для SHaRC.
// Полностью детерминированный пространственно-временной фильтр без кипения.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "nrd_reproj.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(77, 0)]] RWByteAddressBuffer gColor;

struct FlightPush {
    uint flight;
    uint pass;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
FlightPush gPush;

uint svgfStride() {
    return gFrame.extent.x * gFrame.extent.y * 16u;
}

uint svgfAt(int2 ip, uint slice) {
    return slice * svgfStride() + (uint(ip.y) * gFrame.extent.x + uint(ip.x)) * 16u;
}

float3 svgfLoad(uint o) {
    return float3(asfloat(gColor.Load(o)), asfloat(gColor.Load(o + 4u)), asfloat(gColor.Load(o + 8u)));
}

float svgfLoadA(uint o) {
    return asfloat(gColor.Load(o + 12u));
}

void svgfStore(uint o, float3 c, float a) {
    gColor.Store(o, asuint(c.r));
    gColor.Store(o + 4u, asuint(c.g));
    gColor.Store(o + 8u, asuint(c.b));
    gColor.Store(o + 12u, asuint(a));
}

bool svgfInside(int2 p) {
    return p.x >= 0 && p.y >= 0 && p.x < int(gFrame.extent.x) && p.y < int(gFrame.extent.y);
}

bool svgfLive(int2 ip) {
    if (!svgfInside(ip)) {
        return false;
    }
    return gVis.Load(int3(ip, 0)) != 0u && gDepth.Load(int3(ip, 0)) > 0.0;
}

float svgfViewZ(int2 ip) {
    if (!svgfLive(ip)) {
        return -1.0;
    }
    const float3 P = reconstructWorld(uint2(ip), gDepth.Load(int3(ip, 0)));
    return max(length(P - gFrame.cameraPos.xyz), 1e-3);
}

float3 svgfGeoNormal(int2 ip) {
    const float depth = gDepth.Load(int3(ip, 0));
    const float3 P = reconstructWorld(uint2(ip), max(depth, 1e-5));
    float3 geoN = reconstructNormalFromDepth(gDepth, uint2(ip), P);
    if (dot(geoN, geoN) < 0.5) {
        geoN = octDecode(gSurf1[ip].xy);
    }
    geoN = dot(geoN, geoN) > 1e-8 ? normalize(geoN) : float3(0.0, 1.0, 0.0);
    const float3 view = gFrame.cameraPos.xyz - P;
    if (dot(geoN, view) < 0.0) {
        geoN = -geoN;
    }
    return geoN;
}

float3 svgfSample(int2 ip) {
    return max(svgfLoad(svgfAt(ip, 0u)), 0.0);
}

uint svgfReadSlice() {
    return 1u + (gPush.flight & 1u);
}

uint svgfWriteSlice() {
    return 1u + ((gPush.flight ^ 1u) & 1u);
}

float3 svgfSrc(int2 p) {
    return max(svgfLoad(svgfAt(p, svgfWriteSlice())), 0.0);
}

float svgfSrcA(int2 p) {
    return svgfLoadA(svgfAt(p, svgfWriteSlice()));
}

float3 svgfHistBilinear(float2 uv) {
    const float2 p = uv * float2(gFrame.extent.xy) - 0.5;
    const int2 i = int2(floor(p));
    const float2 f = saturate(p - float2(i));
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const uint slice = svgfReadSlice();
    const float3 c00 = max(svgfLoad(svgfAt(clamp(i, int2(0, 0), ext), slice)), 0.0);
    const float3 c10 = max(svgfLoad(svgfAt(clamp(i + int2(1, 0), int2(0, 0), ext), slice)), 0.0);
    const float3 c01 = max(svgfLoad(svgfAt(clamp(i + int2(0, 1), int2(0, 0), ext), slice)), 0.0);
    const float3 c11 = max(svgfLoad(svgfAt(clamp(i + int2(1, 1), int2(0, 0), ext), slice)), 0.0);
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}

void svgfComposite(uint2 px, float3 irradiance) {
    const float3 albedo = max(gSurf0[px].rgb, float3(0.02, 0.02, 0.02));
    const float metal = saturate(gSurf1[px].z);
    const float gain = max(gShade.pad1, 0.0);
    float3 gi = max(irradiance, 0.0) * albedo * (1.0 - metal) * gain;
    if (any(isnan(gi)) || any(isinf(gi))) {
        gi = 0.0;
    }
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(gi * 0.8, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += gi;
    gHdr[px] = hdr;
}

void svgfTemporal(int2 ip) {
    if (!svgfLive(ip)) {
        svgfStore(svgfAt(ip, svgfWriteSlice()), 0.0, 0.0);
        return;
    }
    const float z = svgfViewZ(ip);
    const float3 n = svgfGeoNormal(ip);
    const float3 current = svgfSample(ip);
    const float depth = gDepth.Load(int3(ip, 0));
    const float3 P = reconstructWorld(uint2(ip), depth);
    const float2 prevUv = nrdPrevUv(P);

    float alpha = 0.0;
    float histLen = 1.0;
    float3 hist = current;
    if (nrdInside(prevUv)) {
        const int2 prev = nrdUvPx(prevUv);
        const float histA = svgfLoadA(svgfAt(prev, svgfReadSlice()));
        const float zPrev = svgfViewZ(prev);
        const float nd = dot(n, svgfGeoNormal(prev));
        const float dz = abs(z - zPrev) / max(z, 1e-3);
        if (histA >= 1.0 && zPrev > 1e-3 && nd >= 0.8 && dz <= 0.25) {
            hist = svgfHistBilinear(prevUv);
            histLen = min(histA + 1.0, 64.0);
            alpha = min(histLen / (histLen + 1.0), 0.96);
        }
    }
    const float3 accum = lerp(current, hist, alpha);
    svgfStore(svgfAt(ip, svgfWriteSlice()), max(accum, 0.0), histLen);
}

// Проход 1: Регулярный 3x3 A-Trous фильтр с шагом 4 пикселя (без вращающегося шума!)
void svgfAtrousWide(int2 ip) {
    const float3 center = svgfSrc(ip);
    const float centerA = svgfSrcA(ip);
    if (!svgfLive(ip)) {
        svgfStore(svgfAt(ip, 0u), 0.0, 0.0);
        return;
    }
    const float depth = gDepth.Load(int3(ip, 0));
    const float3 n = svgfGeoNormal(ip);
    float3 acc = center;
    float wsum = 1.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int2 q = ip + int2(dx, dy) * 4;
            if (!svgfInside(q)) {
                continue;
            }
            const float zq = gDepth.Load(int3(q, 0));
            if (zq <= 0.0) {
                continue;
            }
            const float wn = pow(max(dot(n, svgfGeoNormal(q)), 0.0), 8.0);
            const float wd = exp(-abs(depth - zq) / max(depth * 0.04, 1e-4));
            const float wk = (dx == 0 || dy == 0) ? 0.5 : 0.25;
            const float w = wn * wd * wk;
            acc += svgfSrc(q) * w;
            wsum += w;
        }
    }
    svgfStore(svgfAt(ip, 0u), wsum > 1e-5 ? acc / wsum : center, centerA);
}

// Проход 2: Финальный регулярный 3x3 A-Trous фильтр с шагом 1 пиксель и композитинг
void svgfFinalize(uint2 px, int2 ip) {
    if (!svgfLive(ip)) {
        svgfComposite(px, 0.0);
        return;
    }
    const float depth = gDepth.Load(int3(ip, 0));
    const float3 n = svgfGeoNormal(ip);
    const float3 center = max(svgfLoad(svgfAt(ip, 0u)), 0.0);
    float3 acc = center;
    float wsum = 1.0;

    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            const int2 q = ip + int2(dx, dy);
            if (!svgfInside(q)) {
                continue;
            }
            const float zq = gDepth.Load(int3(q, 0));
            if (zq <= 0.0) {
                continue;
            }
            const float wn = pow(max(dot(n, svgfGeoNormal(q)), 0.0), 8.0);
            const float wd = exp(-abs(depth - zq) / max(depth * 0.03, 1e-4));
            const float wk = (dx == 0 || dy == 0) ? 0.5 : 0.25;
            const float w = wn * wd * wk;
            acc += max(svgfLoad(svgfAt(q, 0u)), 0.0) * w;
            wsum += w;
        }
    }
    svgfComposite(px, wsum > 1e-5 ? acc / wsum : center);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const int2 ip = int2(dtid.xy);
    if (gPush.pass == 0u) {
        svgfTemporal(ip);
        return;
    }
    if (gPush.pass == 1u) {
        svgfAtrousWide(ip);
        return;
    }
    svgfFinalize(dtid.xy, ip);
}
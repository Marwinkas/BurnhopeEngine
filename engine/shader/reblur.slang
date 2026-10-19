// SVGF для SHaRC. Цвет — тот же add, что запрос пишет в HDR: три float, смещения 0, 4, 8.
// Срез 0 сырой, срез 1 история, срез 2 накопление. Проекция как в TAA.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "nrd_reproj.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
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

float svgfLuma(float3 c) {
    return dot(max(c, 0.0), float3(0.2126, 0.7152, 0.0722));
}

float svgfKernel(int tap) {
    if (tap == -2 || tap == 2) {
        return 1.0 / 16.0;
    }
    if (tap == -1 || tap == 1) {
        return 4.0 / 16.0;
    }
    return 6.0 / 16.0;
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

float3 svgfNormal(int2 ip) {
    float3 n = octDecode(gSurf1[ip].xy);
    const float depth = gDepth.Load(int3(ip, 0));
    const float3 P = reconstructWorld(uint2(ip), max(depth, 1e-5));
    if (dot(n, n) < 0.5 || depth <= 0.0) {
        return reconstructNormalFromDepth(gDepth, uint2(ip), P);
    }
    n = normalize(n);
    const float3 view = gFrame.cameraPos.xyz - P;
    if (dot(n, view) < 0.0) {
        n = -n;
    }
    return n;
}

float3 svgfSample(int2 ip) {
    return max(svgfLoad(svgfAt(ip, 0u)), 0.0);
}

float3 svgfSrc(int2 p) {
    const uint slice = gPush.pass == 3u ? 0u : 2u;
    return max(svgfLoad(svgfAt(p, slice)), 0.0);
}

float svgfSrcA(int2 p) {
    const uint slice = gPush.pass == 3u ? 0u : 2u;
    return svgfLoadA(svgfAt(p, slice));
}

void svgfComposite(uint2 px, float3 add) {
    float3 gi = max(add, 0.0);
    if (!all(gi == gi)) {
        gi = 0.0;
    }
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(gi.r * 0.8, gi.g * 0.8, gi.b * 0.8, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.r = hdr.r + gi.r;
    hdr.g = hdr.g + gi.g;
    hdr.b = hdr.b + gi.b;
    gHdr[px] = hdr;
}

void svgfTemporal(int2 ip) {
    if (!svgfLive(ip)) {
        svgfStore(svgfAt(ip, 2u), 0.0, 0.0);
        return;
    }
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const float z = svgfViewZ(ip);
    const float3 n = svgfNormal(ip);
    const float sigmaZ = max(z, 0.05) * 0.02;
    float3 sum = 0.0;
    float3 moment = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 q = clamp(ip + int2(x, y), int2(0, 0), ext);
            if ((x != 0 || y != 0) && !svgfLive(q)) {
                continue;
            }
            const float3 c = svgfSample(q);
            const float wn = (x == 0 && y == 0) ? 1.0 : pow(max(dot(n, svgfNormal(q)), 0.0), 128.0);
            const float zq = (x == 0 && y == 0) ? z : svgfViewZ(q);
            const float wd = (x == 0 && y == 0) ? 1.0 : exp(-abs(z - zq) / sigmaZ);
            const float w = wn * wd;
            sum += c * w;
            moment += c * c * w;
            wsum += w;
        }
    }
    const float3 mean = sum / max(wsum, 1e-5);
    const float3 variance = max(moment / max(wsum, 1e-5) - mean * mean, 0.0);
    const float3 sigma = sqrt(variance);
    const float3 lo = max(mean - sigma, 0.0);
    const float3 hi = mean + sigma;
    const float3 clipped = clamp(svgfSample(ip), lo, hi);
    const float spatialVar = svgfLuma(variance);
    const float3 P = reconstructWorld(uint2(ip), gDepth.Load(int3(ip, 0)));
    const float2 prevUv = nrdPrevUv(P);
    float alpha = 0.0;
    float3 hist = clipped;
    float histVar = spatialVar;
    if (nrdInside(prevUv)) {
        const int2 prev = nrdUvPx(prevUv);
        const uint ho = svgfAt(prev, 1u);
        const float histA = svgfLoadA(ho);
        const float zPrev = svgfViewZ(prev);
        const float nd = dot(n, svgfNormal(prev));
        const float dz = abs(z - zPrev) / max(z, 1e-3);
        if (histA > 0.0 && zPrev > 1e-3 && nd >= 0.9 && dz <= 0.05) {
            const float2 currUv = (float2(ip) + 0.5) * gFrame.invExtent;
            const float speed = length((currUv - prevUv) * float2(gFrame.extent.xy));
            alpha = speed > 4.0 ? 0.5 : 0.9;
            hist = clamp(max(svgfLoad(ho), 0.0), lo, hi);
            histVar = max(histA - 1.0, 0.0);
        }
    }
    const float3 accum = lerp(clipped, hist, alpha);
    const float varOut = lerp(spatialVar, histVar, alpha * alpha);
    svgfStore(svgfAt(ip, 2u), max(accum, 0.0), varOut + 1.0);
}

void svgfCommit(int2 ip) {
    if (!svgfLive(ip)) {
        svgfStore(svgfAt(ip, 1u), 0.0, 0.0);
        return;
    }
    const uint src = svgfAt(ip, 2u);
    svgfStore(svgfAt(ip, 1u), svgfLoad(src), svgfLoadA(src));
}

void svgfAtrous(uint2 px, int2 ip) {
    const float3 center = svgfSrc(ip);
    const float centerA = svgfSrcA(ip);
    if (!svgfLive(ip)) {
        if (gPush.pass == 4u) {
            svgfComposite(px, 0.0);
        } else if (gPush.pass == 2u) {
            svgfStore(svgfAt(ip, 0u), 0.0, 0.0);
        } else {
            svgfStore(svgfAt(ip, 2u), 0.0, 0.0);
        }
        return;
    }
    const int step = 1 << int(gPush.pass - 2u);
    const float z = svgfViewZ(ip);
    const float3 n = svgfNormal(ip);
    const float sigmaZ = max(z, 0.05) * 0.01;
    float3 acc = 0.0;
    float wsum = 0.0;
    [loop]
    for (int y = -2; y <= 2; ++y) {
        [loop]
        for (int x = -2; x <= 2; ++x) {
            const int2 q = ip + int2(x, y) * step;
            if (!svgfInside(q)) {
                continue;
            }
            const float zq = svgfViewZ(q);
            if (zq < 0.0) {
                continue;
            }
            const float3 srgb = svgfSrc(q);
            const float wk = svgfKernel(x) * svgfKernel(y);
            const float wn = pow(max(dot(n, svgfNormal(q)), 0.0), 128.0);
            const float wd = exp(-abs(z - zq) / sigmaZ);
            const float w = wk * wn * wd;
            acc += srgb * w;
            wsum += w;
        }
    }
    const float3 blurred = wsum > 1e-5 ? acc / wsum : center;
    if (gPush.pass == 2u) {
        svgfStore(svgfAt(ip, 0u), blurred, centerA);
        return;
    }
    if (gPush.pass == 3u) {
        svgfStore(svgfAt(ip, 2u), blurred, centerA);
        return;
    }
    svgfComposite(px, blurred);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    const int2 ip = int2(dtid.xy);
    if (gPush.pass == 0u) {
        svgfTemporal(ip);
        return;
    }
    if (gPush.pass == 1u) {
        svgfCommit(ip);
        return;
    }
    svgfAtrous(dtid.xy, ip);
}

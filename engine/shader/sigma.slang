// Полутень ReSTIR DI. Сырой луч — 0 или 1. Время копится отдельно от цвета,
// радиус размытия — hitDistance * tan(полудиск солнца), контакт остаётся резким.

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

static const float2 kPoisson[16] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

uint2 sigmaAt(uint slice, uint2 px) {
    return uint2(px.x, px.y + slice * gFrame.extent.y);
}

float sigmaIgn(int2 p) {
    return frac(52.9829189 * frac(0.06711056 * float(p.x) + 0.00583715 * float(p.y)));
}

float sigmaViewZ(int2 ip) {
    if (ip.x < 0 || ip.y < 0 || ip.x >= int(gFrame.extent.x) || ip.y >= int(gFrame.extent.y)) {
        return -1.0;
    }
    const float depth = gDepth.Load(int3(ip, 0));
    if (depth <= 0.0) {
        return -1.0;
    }
    return max(length(reconstructWorld(uint2(ip), depth) - gFrame.cameraPos.xyz), 1e-3);
}

void sigmaComposite(uint2 px, float vis) {
    vis = saturate(vis);
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
    hdr.rgb += max(gSunLit[sigmaAt(gPush.flight, px)].rgb, 0.0) * vis;
    gHdr[px] = hdr;
}

void sigmaTemporal(uint2 px) {
    const uint2 cur = sigmaAt(gPush.flight, px);
    const uint2 dest = sigmaAt(gPush.writeSlice, px);
    const float4 raw = gPenumbra[cur];
    const float depth = gDepth.Load(int3(px, 0));
    if (raw.a < 0.5 || depth <= 0.0) {
        gShadowHist[dest] = 0.0;
        return;
    }
    const int2 ip = int2(px);
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    float vmin = 1.0;
    float vmax = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 q = clamp(ip + int2(x, y), int2(0, 0), ext);
            const float4 nb = gPenumbra[sigmaAt(gPush.flight, uint2(q))];
            if (nb.a < 0.5) {
                continue;
            }
            vmin = min(vmin, saturate(nb.r));
            vmax = max(vmax, saturate(nb.r));
        }
    }
    float vis = saturate(raw.r);
    float pen = max(raw.g, 0.0);
    const float z = sigmaViewZ(ip);
    const float3 n = nrdNormal(gSurf1, gDepth, ip);
    const float3 P = reconstructWorld(px, depth);
    const float2 prevUv = nrdPrevUv(P);
    if (nrdInside(prevUv) && z > 0.0) {
        const int2 prev = nrdUvPx(prevUv);
        const float4 hist = gShadowHist[sigmaAt(gPush.readSlice, uint2(prev))];
        const float zPrev = sigmaViewZ(prev);
        const float nd = dot(n, nrdNormal(gSurf1, gDepth, prev));
        const float dz = abs(z - zPrev) / max(z, 1e-3);
        if (hist.a >= 0.5 && zPrev > 1e-3 && nd >= 0.95 && dz <= 0.02) {
            const float kept = clamp(saturate(hist.r), vmin, vmax);
            vis = lerp(vis, kept, 0.85);
            pen = lerp(pen, max(hist.g, 0.0), 0.85);
        }
    }
    gShadowHist[dest] = float4(saturate(vis), pen, 1.0, 1.0);
}

void sigmaSpatial(uint2 px) {
    const uint2 src = sigmaAt(gPush.writeSlice, px);
    const float4 acc = gShadowHist[src];
    if (acc.a < 0.5) {
        return;
    }
    float vis = saturate(acc.r);
    const float pen = max(acc.g, 0.0);
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0 || pen <= 1e-4 || pen >= 512.0) {
        sigmaComposite(px, vis);
        return;
    }
    const int2 ip = int2(px);
    const float z = sigmaViewZ(ip);
    const float3 n = nrdNormal(gSurf1, gDepth, ip);
    const float3 P = reconstructWorld(px, depth);
    const float3 side = reconstructWorld(uint2(min(px.x + 1u, gFrame.extent.x - 1u), px.y), depth);
    const float pixel = max(length(side - P), z * gFrame.invExtent.x);
    const float radius = clamp(pen / max(pixel, 1e-4), 0.0, 16.0);
    if (radius <= 0.5) {
        sigmaComposite(px, vis);
        return;
    }
    const float zl = sigmaViewZ(ip + int2(-1, 0));
    const float zr = sigmaViewZ(ip + int2(1, 0));
    const float zu = sigmaViewZ(ip + int2(0, -1));
    const float zd = sigmaViewZ(ip + int2(0, 1));
    const float gx = (zl > 0.0 && zr > 0.0) ? (zr - zl) * 0.5 : 0.0;
    const float gy = (zu > 0.0 && zd > 0.0) ? (zd - zu) * 0.5 : 0.0;
    const float ang = sigmaIgn(ip) * 6.2831853;
    const float cs = cos(ang);
    const float sn = sin(ang);
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    float sum = vis;
    float wsum = 1.0;
    [unroll]
    for (int i = 0; i < 16; ++i) {
        const float2 disk = kPoisson[i];
        const float2 rot = float2(cs * disk.x - sn * disk.y, sn * disk.x + cs * disk.y);
        const int2 tap = clamp(ip + int2(round(rot * radius)), int2(0, 0), ext);
        const float4 nb = gShadowHist[sigmaAt(gPush.writeSlice, uint2(tap))];
        if (nb.a < 0.5) {
            continue;
        }
        const float2 off = float2(tap - ip);
        const float zq = sigmaViewZ(tap);
        if (zq < 0.0) {
            continue;
        }
        const float plane = z + gx * off.x + gy * off.y;
        const float wn = pow(max(dot(n, nrdNormal(gSurf1, gDepth, tap)), 0.0), 32.0);
        const float wd = exp(-abs(zq - plane) / (0.01 * z + 0.001));
        const float ws = exp(-dot(disk, disk));
        const float w = ws * wn * wd;
        sum += saturate(nb.r) * w;
        wsum += w;
    }
    sigmaComposite(px, sum / max(wsum, 1e-4));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
        return;
    }
    if (gPush.pass == 0u) {
        sigmaTemporal(px);
        return;
    }
    sigmaSpatial(px);
}

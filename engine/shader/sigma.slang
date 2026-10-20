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

uint2 sigmaAt(uint slice, uint2 px) {
    return uint2(px.x, px.y + slice * gFrame.extent.y);
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
    float mean = 0.0;
    float moment = 0.0;
    float count = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 q = clamp(ip + int2(x, y), int2(0, 0), ext);
            const float4 nb = gPenumbra[sigmaAt(gPush.flight, uint2(q))];
            if (nb.a < 0.5) {
                continue;
            }
            const float s = saturate(nb.r);
            mean += s;
            moment += s * s;
            count += 1.0;
        }
    }
    float dev = 0.0;
    float current = saturate(raw.r);
    if (count >= 4.0) {
        mean /= count;
        dev = sqrt(max(moment / count - mean * mean, 0.0));
        current = clamp(current, mean - dev * 1.5, mean + dev * 1.5);
    } else if (count > 0.0) {
        mean /= count;
    }
    float pen = raw.g >= 512.0 ? 0.0 : max(raw.g, 0.0);
    float vis = saturate(current);
    float histLen = 1.0;
    const float z = sigmaViewZ(ip);
    const float3 n = nrdNormal(gSurf1, gDepth, ip);
    const float3 P = reconstructWorld(px, depth);
    const float2 prevUv = nrdPrevUv(P);
    if (nrdInside(prevUv) && z > 0.0) {
        const int2 prev = nrdUvPx(prevUv);
        const float4 hist = gShadowHist[sigmaAt(gPush.readSlice, uint2(prev))];
        const float zPrev = sigmaViewZ(prev);
        const float3 toCam = gFrame.cameraPos.xyz - P;
        const float facing = saturate(dot(n, dot(toCam, toCam) > 1e-8 ? normalize(toCam) : n));
        const float depthTol = lerp(0.06, 0.02, facing);
        const float nd = dot(n, nrdNormal(gSurf1, gDepth, prev));
        const float dz = abs(z - zPrev) / max(z, 1e-3);
        if (hist.a >= 0.5 && zPrev > 1e-3 && nd >= 0.95 && dz <= depthTol) {
            histLen = clamp(hist.b, 1.0, 8.0);
            const float kept = saturate(hist.r);
            const float weight = histLen / (histLen + 1.0);
            vis = lerp(vis, kept, weight);
            const float histPen = hist.g >= 512.0 ? 0.0 : max(hist.g, 0.0);
            pen = lerp(pen, histPen, weight);
            histLen = min(histLen + 1.0, 8.0);
        }
    }
    const float band = max(dev * 2.0, 0.12);
    vis = clamp(vis, mean - band, mean + band);
    gShadowHist[dest] = float4(saturate(vis), pen, histLen, 1.0);
}

void sigmaSpatial(uint2 px) {
    const uint2 src = sigmaAt(gPush.writeSlice, px);
    const float4 acc = gShadowHist[src];
    if (acc.a < 0.5) {
        return;
    }
    float vis = saturate(acc.r);
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        return;
    }
    const int2 ip = int2(px);
    const float z = sigmaViewZ(ip);
    const float3 n = nrdNormal(gSurf1, gDepth, ip);
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const int2 cross[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
    float sum = vis;
    float wsum = 1.0;
    float vmin = vis;
    float vmax = vis;
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        const int2 q = clamp(ip + cross[i], int2(0, 0), ext);
        const float4 nb = gShadowHist[sigmaAt(gPush.writeSlice, uint2(q))];
        if (nb.a < 0.5) {
            continue;
        }
        const float zq = sigmaViewZ(q);
        if (zq < 0.0) {
            continue;
        }
        const float sample = saturate(nb.r);
        vmin = min(vmin, sample);
        vmax = max(vmax, sample);
        const float wn = pow(max(dot(n, nrdNormal(gSurf1, gDepth, q)), 0.0), 32.0);
        const float dz = abs(z - zq) / max(z * 0.02, 1e-3);
        const float w = wn * exp(-dz * dz);
        sum += sample * w;
        wsum += w;
    }
    const float histLen = clamp(acc.b, 1.0, 8.0);
    if (vmax - vmin > 0.2 || histLen < 4.0) {
        const int reach = histLen < 4.0 ? 3 : 2;
        [unroll]
        for (uint i = 0u; i < 4u; ++i) {
            const int2 q = clamp(ip + cross[i] * reach, int2(0, 0), ext);
            const float4 nb = gShadowHist[sigmaAt(gPush.writeSlice, uint2(q))];
            if (nb.a < 0.5) {
                continue;
            }
            const float zq = sigmaViewZ(q);
            if (zq < 0.0) {
                continue;
            }
            const float wn = pow(max(dot(n, nrdNormal(gSurf1, gDepth, q)), 0.0), 32.0);
            const float dz = abs(z - zq) / max(z * 0.02, 1e-3);
            const float w = wn * exp(-dz * dz) * 0.5;
            sum += saturate(nb.r) * w;
            wsum += w;
        }
    }
    sigmaComposite(px, wsum > 1e-4 ? saturate(sum / wsum) : vis);
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

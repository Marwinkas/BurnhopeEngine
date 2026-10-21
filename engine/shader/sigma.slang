// Полутень ReSTIR DI (Contact-Hardening À-Trous SVGF / REBLUR Denoiser).
// Временная история хранит (vis, penumbraPx, histLen, viewZ).
// На контакте (penumbraPx -> 0) тень остаётся бритвенно-резкой, вдали — мягко фильтруется без шума.

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

float3 sigmaNormal(int2 ip, float depth, float3 P) {
    if (depth <= 0.0) {
        return float3(0.0, 1.0, 0.0);
    }
    return nrdNormal(gSurf1, gDepth, ip);
}

float sigmaSame(float3 n, float3 P, float3 Q) {
    const float span = max(length(Q - P), 1e-3);
    const float gap = abs(dot(n, Q - P));
    return exp(-pow(gap / max(span * 0.2, 0.015), 2.0));
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
    float penSum = 0.0;
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
            penSum += clamp(nb.g, 0.0, 16.0);
            count += 1.0;
        }
    }

    float dev = 0.0;
    float current = saturate(raw.r);
    float pen = clamp(raw.g, 0.0, 16.0);
    if (count > 0.0) {
        mean /= count;
        dev = sqrt(max(moment / count - mean * mean, 0.0));
        pen = max(pen, penSum / count);
    }

    float vis = current;
    float histLen = 1.0;
    const float z = sigmaViewZ(ip);
    const float3 P = reconstructWorld(px, depth);
    const float2 prevUv = nrdPrevUv(P);

    if (nrdInside(prevUv) && z > 0.0) {
        const int2 prev = nrdUvPx(prevUv);
        const float4 hist = gShadowHist[sigmaAt(gPush.readSlice, uint2(prev))];
        const float prevZ = hist.a;
        const float dz = abs(z - prevZ) / max(z, 1e-3);
        if (prevZ > 0.0 && dz <= 0.05) {
            const float kept = saturate(hist.r);
            if (kept > current + 0.35) {
                vis = current;
                pen = clamp(raw.g, 0.0, 16.0);
                histLen = 1.0;
            } else {
                histLen = clamp(hist.b, 1.0, 24.0);
                const float weight = histLen / (histLen + 1.0);
                vis = lerp(vis, kept, weight);
                const float histPen = clamp(hist.g, 0.0, 16.0);
                pen = lerp(pen, histPen, weight);
                histLen = min(histLen + 1.0, 24.0);
            }
        }
    }

    const float band = max(dev * 2.5, 0.15);
    vis = count > 0.0 ? clamp(vis, mean - band, mean + band) : vis;

    // В альфа-канал записываем линейную глубину текущего кадра z для честного сравнения в следующем кадре
    gShadowHist[dest] = float4(saturate(vis), pen, histLen, z);
}

void sigmaSpatial(uint2 px) {
    const uint2 src = sigmaAt(gPush.writeSlice, px);
    const float4 acc = gShadowHist[src];
    const float depth = gDepth.Load(int3(px, 0));
    if (acc.a <= 0.0 || depth <= 0.0) {
        return;
    }

    float vis = saturate(acc.r);
    const float pen = clamp(acc.g, 0.0, 16.0);
    const float histLen = clamp(acc.b, 1.0, 24.0);

    // На контакте (pen -> 0) при накопленной истории не размываем тень вообще — сохраняем максимальную четкость
    const float disoccBoost = histLen < 4.0 ? 2.0 : 1.0;
    const float filterRadius = pen * disoccBoost;
    if (filterRadius < 0.2 && histLen >= 4.0) {
        sigmaComposite(px, vis);
        return;
    }

    const int2 ip = int2(px);
    const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
    const float3 P = reconstructWorld(px, depth);
    const float3 n = sigmaNormal(ip, depth, P);

    // Шаг фильтрации плавно растет с шириной полутени (Contact-Hardening)
    const float stepScale = clamp(filterRadius * 0.35, 1.0, 3.0);
    const float kKernel[5] = { 1.0 / 16.0, 4.0 / 16.0, 6.0 / 16.0, 4.0 / 16.0, 1.0 / 16.0 };

    float sum = vis * (kKernel[2] * kKernel[2]);
    float wsum = kKernel[2] * kKernel[2];

    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            if (x == 0 && y == 0) {
                continue;
            }
            const int2 offset = int2(round(float2(float(x), float(y)) * stepScale));
            const int2 q = clamp(ip + offset, int2(0, 0), ext);
            const float4 nb = gShadowHist[sigmaAt(gPush.writeSlice, uint2(q))];
            if (nb.a <= 0.0) {
                continue;
            }
            const float dq = gDepth.Load(int3(q, 0));
            if (dq <= 0.0) {
                continue;
            }
            const float3 Q = reconstructWorld(uint2(q), dq);
            const float3 nq = sigmaNormal(q, dq, Q);

            const float kw = kKernel[x + 2] * kKernel[y + 2];
            const float wn = pow(saturate(dot(n, nq)), 32.0);
            const float wp = sigmaSame(n, P, Q);
            const float w = kw * wn * wp;

            sum += saturate(nb.r) * w;
            wsum += w;
        }
    }

    sigmaComposite(px, wsum > 1e-5 ? saturate(sum / wsum) : vis);
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
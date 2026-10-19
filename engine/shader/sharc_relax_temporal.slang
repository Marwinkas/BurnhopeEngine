#include "frame.slang"
#include "core/math.slang"
#include "camera.slang"
#include "sharc_params.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(23, 0)]] SamplerState gLinear;
[[vk::binding(73, 0)]] ConstantBuffer<SharcGpuParams> gSharc;
[[vk::binding(74, 0)]] RWTexture2D<float4> gRaw;
[[vk::binding(75, 0)]] Texture2D<float4> gHist;
[[vk::binding(76, 0)]] RWTexture2D<float4> gOut;

// RELAX Anti-Firefly + TemporalAccumulation.
// Альфа — длина истории 1..32. Цвет истории зажимается по дисперсии окрестности,
// без отсечения по оттенку: иначе первый красный семпл не пускает зелёный и синий.

float3 sharcNormal(uint2 px, float3 position) {
    float3 normal = octDecode(gSurf1[px].xy);
    if (dot(normal, normal) < 0.5) {
        normal = reconstructNormalFromDepth(gDepth, px, position);
    }
    return normalize(normal);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void sharcRelaxTemporal(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    uint2 px = dtid.xy;
    float4 raw = gRaw[px];
    float3 mean = 0.0;
    float3 moment = 0.0;
    float count = 0.0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy) {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx) {
            int2 q = int2(px) + int2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(gFrame.extent.x) || q.y >= int(gFrame.extent.y)) {
                continue;
            }
            float4 tap = gRaw[uint2(q)];
            if (tap.a < 0.5) {
                continue;
            }
            mean += tap.rgb;
            moment += tap.rgb * tap.rgb;
            count += 1.0;
        }
    }
    float3 sigma = 0.0;
    if (count > 0.0) {
        mean /= count;
        sigma = sqrt(max(moment / count - mean * mean, 0.0));
    }
    float3 current = count > 0.0 ? mean : max(raw.rgb, 0.0);
    if (count >= 4.0 && raw.a > 0.5) {
        current = clamp(max(raw.rgb, 0.0), mean - sigma * 1.5, mean + sigma * 1.5);
    }
    float depth = gDepth.Load(int3(px, 0));
    float3 position = depth > 0.0 ? reconstructWorld(px, depth) : gFrame.cameraPos.xyz;
    float3 normal = depth > 0.0 ? sharcNormal(px, position) : float3(0.0, 1.0, 0.0);
    float4 hist = 0.0;
    bool accept = false;
    if (gSharc.frameIndex > 0u && depth > 0.0) {
        float4 prevClip = mul(gSharc.prevViewProj, float4(position, 1.0));
        if (prevClip.w > 1e-4) {
            float2 prevUv = prevClip.xy / prevClip.w * 0.5 + 0.5;
            if (prevUv.x > 0.0 && prevUv.y > 0.0 && prevUv.x < 1.0 && prevUv.y < 1.0) {
                hist = gHist.SampleLevel(gLinear, prevUv, 0.0);
                uint2 prevPx = min(uint2(prevUv * float2(gFrame.extent)), gFrame.extent - 1u);
                float prevDepth = gDepth.Load(int3(prevPx, 0));
                if (prevDepth > 0.0 && hist.a >= 1.0) {
                    float3 prevPos = reconstructWorld(prevPx, prevDepth);
                    float3 prevN = sharcNormal(prevPx, prevPos);
                    float z0 = length(gFrame.cameraPos.xyz - position);
                    float z1 = length(gFrame.cameraPos.xyz - prevPos);
                    accept = dot(normal, prevN) > 0.9 && abs(z0 - z1) < max(z0, 0.05) * 0.05;
                }
            }
        }
    }
    float3 color = max(current, 0.0);
    float lengthHist = raw.a > 0.5 || count > 0.0 ? 1.0 : 0.0;
    if (accept) {
        float3 kept = max(hist.rgb, 0.0);
        if (count >= 4.0) {
            kept = clamp(kept, mean - sigma * 2.0, mean + sigma * 2.0);
        }
        float histLen = min(hist.a, 32.0);
        float weight = histLen / (histLen + 1.0);
        color = lerp(color, kept, weight);
        lengthHist = min(histLen + 1.0, 32.0);
    }
    gOut[px] = float4(max(color, 0.0), lengthHist);
}

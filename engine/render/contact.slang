// Контактная тень в R8. Солнце только умножает готовое значение.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(46, 0)]] RWTexture2D<float> gContact;

float blueNoise64(uint2 p) {
    uint n = (p.x & 63u) + (p.y & 63u) * 64u;
    n = (n << 13u) ^ n;
    n = n * (n * n * 15731u + 789221u) + 1376312589u;
    return float(n & 0x7fffffffu) / 2147483647.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void contactCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gVis.Load(int3(px, 0)) == 0u || gShade.sscsAmt <= 0.001 || gFrame.disableShadow != 0) {
        gContact[px] = 1.0;
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        gContact[px] = 1.0;
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 N = reconstructNormalFromDepth(gDepth, px, P);
    const float3 L = normalize(gSun.sunDirIntensity.xyz);
    if (dot(N, L) < 0.05) {
        gContact[px] = 1.0;
        return;
    }
    const float reach = max(gShade.sscsReach, 0.05);
    const float thick = max(gShade.sscsThick, 0.01);
    const float pxCap = clamp(gShade.sscsPx, 4.0, 256.0);
    const int steps = int(clamp(gShade.sscsSteps, 4.0, 16.0));
    const float jitter = blueNoise64(px);
    const float2 uv0 = (float2(px) + 0.5) * gFrame.invExtent;
    const float edge = min(min(uv0.x, 1.0 - uv0.x), min(uv0.y, 1.0 - uv0.y));
    float occ = 1.0;
    [loop]
    for (int s = 1; s <= steps; ++s) {
        float t = reach * (float(s) / float(steps));
        if (s == 1) {
            t *= 0.35 + 0.65 * jitter;
        } else {
            t = reach * pow(float(s) / float(steps), 1.6);
        }
        const float3 Q = P + L * t + N * 0.005;
        const float4 clip = worldToClip(Q);
        if (clip.w <= 1e-3) {
            break;
        }
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
            break;
        }
        const float2 sp = uv * float2(gFrame.extent.xy);
        const float pix = length(sp - float2(px));
        if (pix < 1.5 || pix > pxCap) {
            continue;
        }
        const float sd = gDepth.Load(int3(int2(sp), 0));
        if (sd <= 0.0) {
            continue;
        }
        const float3 sceneP = reconstructWorld(uint2(sp), sd);
        const float along = dot(sceneP - P, L);
        if (along < 0.03 || along >= reach) {
            continue;
        }
        const float side = length(sceneP - P - L * along);
        if (side > thick) {
            continue;
        }
        const float hand = saturate((along - 0.05) / 0.15);
        occ = min(occ, hand);
        break;
    }
    const float border = saturate(edge / 0.04);
    gContact[px] = lerp(1.0, occ, saturate(gShade.sscsAmt) * border);
}

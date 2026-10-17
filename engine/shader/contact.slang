// Контактная тень в R8. Солнце только умножает готовое значение.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(46, 0)]] RWTexture2D<float> gContact;

#include "core/vis_surface.slang"

float ignContact(uint2 px) {
    return frac(52.9829189 * frac(0.06711056 * float(px.x) + 0.00583715 * float(px.y)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void contactCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    const uint centerVis = gVis.Load(int3(px, 0));
    if ((gPass.passMask & 16384u) == 0u || centerVis == 0u || gShade.sscsAmt <= 0.001 || gFrame.disableShadow != 0) {
        gContact[px] = 1.0;
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 1e-6) {
        gContact[px] = 1.0;
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 toCam = gFrame.cameraPos.xyz - P;
    const float viewDist = max(length(toCam), 0.1);
    const float distFade = 1.0 - saturate((viewDist - 24.0) / 12.0);
    if (distFade <= 0.001) {
        gContact[px] = 1.0;
        return;
    }

    const uint packed = centerVis - 1u;
    const VisTri tri = loadVisTri(packed & 0xFFFFFu, packed >> 20u, P);
    float3 N = tri.faceN;
    const float3 V = toCam / viewDist;
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 L = normalize(gSun.sunDirIntensity.xyz);
    const float NdotL = dot(N, L);
    if (NdotL <= 0.06) {
        gContact[px] = 1.0;
        return;
    }

    const float mPerPx = viewDist * gFrame.invExtent.y * 2.0;
    const float maxPx = clamp(gShade.sscsPx, 16.0, 256.0);
    const float reach = clamp(min(gShade.sscsReach, maxPx * mPerPx), 0.04, 1.2);
    const int steps = int(clamp(gShade.sscsSteps, 8.0, 16.0));

    // Reject hits lying on the receiver's own triangle plane (eliminates stair-stepping)
    const float planeTolerance = max(mPerPx * 2.0, 0.005);
    const float depthBias = max(mPerPx * 1.5, 0.002 * viewDist);
    const float maxThick = max(gShade.sscsThick * 10.0, reach * 0.9);
    const float jitter = ignContact(px);

    float occ = 1.0;
    [loop]
    for (int s = 1; s <= steps; ++s) {
        const float sFrac = (float(s) - 0.5 + (jitter - 0.5) * 0.5) / float(steps);
        const float t = sFrac * reach;
        const float3 Q = P + L * t;
        const float4 clipQ = worldToClip(Q);
        if (clipQ.w <= 1e-3) {
            break;
        }
        const float2 uv = clipQ.xy / clipQ.w * 0.5 + 0.5;
        if (uv.x <= 0.005 || uv.y <= 0.005 || uv.x >= 0.995 || uv.y >= 0.995) {
            break;
        }
        const uint2 sp = uint2(uv * float2(gFrame.extent.xy));
        const uint sampleVis = gVis.Load(int3(sp, 0));
        if (sampleVis == 0u || sampleVis == centerVis) {
            continue;
        }
        const float sd = gDepth.Load(int3(sp, 0));
        if (sd <= 1e-6) {
            continue;
        }
        const float3 sceneP = reconstructWorld(sp, sd);

        // Must be above the receiver's surface plane so flat walls never self-occlude
        if (dot(sceneP - P, N) <= planeTolerance) {
            continue;
        }

        const float4 clipScene = worldToClip(sceneP);
        const float depthDelta = clipQ.w - clipScene.w;
        if (depthDelta > depthBias && depthDelta < maxThick) {
            if (length(sceneP - Q) < maxThick * 1.25) {
                const float fade = saturate(1.0 - sFrac * 0.65);
                occ = min(occ, 1.0 - fade);
                break;
            }
        }
    }

    gContact[px] = lerp(1.0, occ, saturate(gShade.sscsAmt) * distFade * saturate((NdotL - 0.06) * 6.0));
}
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
    const uint packed = gVis.Load(int3(px, 0)) - 1u;
    const VisTri tri = loadVisTri(packed & 0xFFFFFu, packed >> 20u, P);
    float3 N = tri.faceN;
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 L = normalize(gSun.sunDirIntensity.xyz);
    if (dot(N, L) < 0.05) {
        gContact[px] = 1.0;
        return;
    }
    const float reach = max(gShade.sscsReach, 0.05);
    const float thick = max(gShade.sscsThick, 0.01);
    const int steps = int(clamp(gShade.sscsSteps, 4.0, 16.0));
    const float jitter = frac(dot(P, float3(0.754877666, 0.569840296, 0.381966011)));
    const float step = reach / float(steps);
    float occ = 1.0;
    [loop]
    for (int s = 1; s <= steps; ++s) {
        const float t = step * (float(s) - 0.5 + jitter);
        const float3 Q = P + L * t + N * thick;
        const float4 clip = worldToClip(Q);
        if (clip.w <= 1e-3) {
            break;
        }
        const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
            break;
        }
        const uint2 sp = uint2(uv * float2(gFrame.extent.xy));
        const float sd = gDepth.Load(int3(sp, 0));
        if (sd <= 0.0) {
            continue;
        }
        const float3 sceneP = reconstructWorld(sp, sd);
        const float along = dot(sceneP - P, L);
        if (along < step || along >= reach) {
            continue;
        }
        const float side = length(sceneP - P - L * along);
        if (side > thick) {
            continue;
        }
        occ = min(occ, saturate(along / reach));
        break;
    }
    gContact[px] = lerp(1.0, occ, saturate(gShade.sscsAmt));
}

// Filament screenSpaceContactShadow. Луч в экранном пространстве, сравнение с глубиной.

#include "frame.slang"
#include "camera.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(13, 0)]] SamplerState gNearest;
[[vk::binding(46, 0)]] RWTexture2D<float> gContact;

float ignContact(float2 px) {
    const float3 m = float3(0.06711056, 0.00583715, 52.9829189);
    return frac(m.z * frac(dot(px, m.xy)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void contactCs(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if ((gPass.passMask & 16384u) == 0u || gShade.sscsAmt <= 0.001 || gFrame.disableShadow != 0u) {
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
    const float viewDist = max(length(toCam), 0.05);
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    const float3 L = dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);

    const int steps = int(clamp(gShade.sscsSteps, 1.0, 32.0));
    const float reach = clamp(gShade.sscsReach, 0.02, 16.0);
    const float dt = 1.0 / float(steps);
    const float3 wsEnd = P + L * reach;
    const float4 csStart = worldToClip(P);
    const float4 csEnd = worldToClip(wsEnd);
    if (csStart.w <= 1e-4 || csEnd.w <= 1e-4) {
        gContact[px] = 1.0;
        return;
    }
    const float3 ssStart = csStart.xyz / csStart.w;
    const float3 ssEnd = csEnd.xyz / csEnd.w;
    const float3 uvStart = float3(ssStart.xy * 0.5 + 0.5, ssStart.z);
    const float3 uvEnd = float3(ssEnd.xy * 0.5 + 0.5, ssEnd.z);
    const float3 uvRay = uvEnd - uvStart;
    const float3 viewDir = -toCam / viewDist;
    const float4 csViewEnd = worldToClip(P + viewDir * reach);
    const float viewZ = csViewEnd.z / max(csViewEnd.w, 1e-4);
    const float tolerance = max(abs(viewZ - ssStart.z) * dt, 1e-5);

    const float dither = ignContact(float2(px) + 0.5) - 0.5;
    float t = dt * dither + dt;
    float occlusion = 0.0;
    float2 edgeUv = uvStart.xy;
    [loop]
    for (int i = 0; i < steps; ++i) {
        const float3 ray = uvStart + uvRay * t;
        edgeUv = ray.xy;
        if (ray.x <= 0.0 || ray.y <= 0.0 || ray.x >= 1.0 || ray.y >= 1.0) {
            break;
        }
        const float z = gDepth.SampleLevel(gNearest, ray.xy, 0.0);
        const float dz = z - ray.z;
        if (abs(tolerance - dz) < tolerance) {
            occlusion = 1.0;
            break;
        }
        t += dt;
    }
    const float2 fade = max(12.0 * abs(edgeUv - 0.5) - 5.0, 0.0);
    occlusion *= saturate(1.0 - dot(fade, fade));
    gContact[px] = 1.0 - occlusion * saturate(gShade.sscsAmt);
}

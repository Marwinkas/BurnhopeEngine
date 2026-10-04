// Солнце пишет HdrA. Лампы, туман и небо здесь не считаются.

#include "frame.slang"
#include "camera.slang"
#include "shade_hit.slang"

[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(15, 0)]] Texture2D<float> gShadow0;
[[vk::binding(16, 0)]] Texture2D<float4> gCube;

float3 sampleEnv(float3 dir) {
    uint face = 0u;
    float2 uv = 0.0;
    cubeDirToFaceUV(dir, face, uv);
    return gCube.SampleLevel(gWrap, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

#include "shade_shadow.slang"
#include "core/atmosphere.slang"

[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;
[[vk::binding(23, 0)]] SamplerState gClamp;

float3 shadowPoint(uint cascade, float3 P, float3 N, float NdotL) {
    const float texel = max(gSun.sunSplit[cascade], 1.0) / 2048.0;
    const float slope = sqrt(saturate(1.0 - NdotL * NdotL));
    return P + N * texel * slope;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void shadeSun(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex) {
    uint vis;
    uint2 px;
    if (!shadeTile(dtid, gtid, vis, px)) {
        return;
    }
    const ShadeHit hit = shadeHit(px, vis, gtid, true);
    gSurf0[px] = float4(hit.albedo, hit.roughness);
    gSurf1[px] = float4(octEncode(hit.N), hit.metallic, hit.ao);
    uint sunCascade = 2u;
    float shadow = 1.0;
    if (gFrame.disableShadow == 0) {
        const float3 p0 = shadowPoint(0u, hit.P, hit.N, hit.NdotL);
        const float3 p1 = shadowPoint(1u, hit.P, hit.N, hit.NdotL);
        const float3 p2 = shadowPoint(2u, hit.P, hit.N, hit.NdotL);
        const float o0 = cascadeOutside(0u, p0);
        const float o1 = cascadeOutside(1u, p1);
        const float s2 = cascadeShadow(2u, p2);
        shadow = s2;
        sunCascade = 2u;
        if (o1 <= 0.0) {
            const float w = saturate(-o1 / 0.18);
            shadow = lerp(s2, cascadeShadow(1u, p1), w);
            if (w > 0.5) {
                sunCascade = 1u;
            }
        }
        if (o0 <= 0.0) {
            const float w = saturate(-o0 / 0.18);
            shadow = lerp(shadow, cascadeShadow(0u, p0), w);
            if (w > 0.5) {
                sunCascade = 0u;
            }
        }
    }
    if (gShade.sscsAmt > 0.001 && hit.NdotL > 0.05) {
        const float reach = max(gShade.sscsReach, 0.05);
        const float thick = max(gShade.sscsThick, 0.01);
        const float pxCap = clamp(gShade.sscsPx, 4.0, 256.0);
        const int steps = int(clamp(gShade.sscsSteps, 2.0, 32.0));
        const float4 aClip = worldToClip(hit.P + hit.N * 0.02);
        if (aClip.w > 1e-3) {
            const float2 px0 = (aClip.xy / aClip.w * 0.5 + 0.5) * float2(gFrame.extent.xy);
            [loop]
            for (int s = 1; s <= steps; ++s) {
                const float t = reach * (float(s) / float(steps));
                const float3 Q = hit.P + hit.L * t + hit.N * 0.02;
                const float4 clip = worldToClip(Q);
                if (clip.w <= 1e-3) {
                    break;
                }
                const float2 uv = clip.xy / clip.w * 0.5 + 0.5;
                if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
                    break;
                }
                const float2 sp = uv * float2(gFrame.extent.xy);
                if (length(sp - px0) > pxCap) {
                    break;
                }
                const float sd = gDepth.Load(int3(int2(sp), 0));
                if (sd <= 0.0) {
                    continue;
                }
                const float3 sceneP = reconstructWorld(uint2(sp), sd);
                const float4 sceneClip = worldToClip(sceneP);
                const float gap = clip.w - sceneClip.w;
                const float along = dot(sceneP - hit.P, hit.L);
                const float bias = min(0.05, thick * 0.3);
                if (gap > bias && gap < thick && along > bias && along < reach) {
                    shadow *= lerp(1.0, 0.0, saturate(gShade.sscsAmt) * saturate(1.0 - along / reach));
                    break;
                }
            }
        }
    }
    shadow *= hit.pomShadow;
    if (gFrame.disableShadow != 0) {
        shadow = 1.0;
    }
    const float3 radiance = gSun.sunColor.xyz * gSun.sunDirIntensity.w;
    float3 color = hit.albedo * shAmbient(hit.N, hit.P) * hit.ao + hit.emissive
        + (hit.diff * hit.ao + hit.spec) * hit.NdotL * shadow * radiance;
    const float gloss = saturate(1.0 - hit.roughness);
    color += sampleEnv(boxProjectDir(hit.P, reflect(-hit.V, hit.N))) * hit.spec * gloss;
    color += hit.albedo * gSkyView.SampleLevel(gClamp, atmSkyUv(hit.N), 0.0).rgb * saturate(hit.N.y) * 0.12 * hit.ao;
    if (gFrame.debugMode == 1) {
        const uint h = hit.tri * 1664525u + 1013904223u;
        gHdr[px] = float4(float(h & 255u) / 255.0, float((h >> 8u) & 255u) / 255.0, float((h >> 16u) & 255u) / 255.0, 1.0);
        return;
    }
    if (gFrame.debugMode == 2) {
        gHdr[px] = float4(hit.N * 0.5 + 0.5, 1.0);
        return;
    }
    if (gFrame.debugMode == 5) {
        float3 tint = float3(1.0, 1.0, 1.0);
        if (sunCascade < 3u) {
            tint = sunCascade == 0u ? float3(1.0, 0.0, 0.0) : sunCascade == 1u ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
        }
        gHdr[px] = float4(tint, 1.0);
        return;
    }
    gHdr[px] = float4(color, hit.roughness);
}

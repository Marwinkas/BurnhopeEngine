// Солнце пишет HdrA. Лампы, туман и небо здесь не считаются.

#include "frame.slang"
#include "camera.slang"
#include "shade_hit.slang"

[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(15, 0)]] Texture2D<float> gShadow0;
[[vk::binding(16, 0)]] Texture2D<float4> gCube;

float3 sampleEnvDir(float3 dir) {
    uint face = 0u;
    float2 uv = 0.0;
    cubeDirToFaceUV(dir, face, uv);
    return gCube.SampleLevel(gWrap, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

float3 sampleEnv(float3 dir, float roughness) {
    const float radius = ggxCone(roughness);
    float3 acc = sampleEnvDir(dir);
    if (radius < 0.002) {
        return acc;
    }
    const float3 up = abs(dir.y) < 0.9 ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    const float3 t = normalize(cross(up, dir));
    const float3 b = cross(dir, t);
    acc += sampleEnvDir(normalize(dir + t * radius));
    acc += sampleEnvDir(normalize(dir - t * radius));
    acc += sampleEnvDir(normalize(dir + b * radius));
    acc += sampleEnvDir(normalize(dir - b * radius));
    return acc * 0.2;
}

#include "shade_shadow.slang"
#include "core/atmosphere.slang"

[[vk::binding(42, 0)]] Texture2D<float4> gSkyView;
[[vk::binding(23, 0)]] SamplerState gClamp;
[[vk::binding(47, 0)]] Texture2D<float> gContact;

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
    if ((gPass.passMask & 16384u) != 0u && gShade.sscsAmt > 0.001 && hit.NdotL > 0.05) {
        shadow *= gContact.Load(int3(px, 0));
    }
    if (gFrame.debugMode == 6) {
        const float c = gContact.Load(int3(px, 0));
        gHdr[px] = float4(c, c, c, 1.0);
        return;
    }
    shadow *= hit.pomShadow;
    if (gFrame.disableShadow != 0) {
        shadow = 1.0;
    }
    float3 radiance = gSun.sunColor.xyz * gSun.sunDirIntensity.w;
    if ((gPass.passMask & 256u) == 0u) {
        radiance = 0.0;
    }
    const float3 sky = gSkyView.SampleLevel(gClamp, atmSkyUv(hit.N), 0.0).rgb;
    const float3 skyFill = sky * saturate(0.35 + 0.65 * hit.N.y) * 0.15 * hit.ao;
    const bool worldRc = gPass.lightProfile >= 1u && (gPass.passMask & 128u) != 0u;
    float3 bounce = shAmbient(hit.N, hit.P);
    if (worldRc) {
        bounce *= 0.35;
    }
    float3 indirect = hit.albedo * bounce * hit.ao + skyFill * (worldRc ? 0.25 : 1.0) + hit.emissive;
    if (gFrame.debugMode == 4) {
        gHdr[px] = float4(max(indirect, 0.0), 1.0);
        return;
    }
    float3 direct = (hit.diff * hit.ao + hit.spec) * hit.NdotL * shadow * radiance;
    if (!all(direct == direct)) {
        direct = 0.0;
    }
    if (gFrame.debugMode == 8u) {
        gHdr[px] = float4(direct, 1.0);
        return;
    }
    if (gFrame.debugMode == 9u) {
        gHdr[px] = float4(hit.albedo, 1.0);
        return;
    }
    if (gFrame.debugMode == 10u) {
        gHdr[px] = float4(hit.NdotL * shadow * radiance, 1.0);
        return;
    }
    float3 color = indirect + direct;
    const float gloss = saturate(1.0 - hit.roughness);
    const float3 env = sampleEnv(boxProjectDir(hit.P, reflect(-hit.V, hit.N)), hit.roughness) * hit.spec * gloss;
    if (all(env == env)) {
        color += max(env, 0.0);
    }
    if (hit.mirror > 0.5) {
        const float3 floorR = float3(-hit.V.x, hit.V.y, -hit.V.z);
        color += sampleEnv(boxProjectDir(hit.P, floorR), hit.roughness) * hit.spec;
    }
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

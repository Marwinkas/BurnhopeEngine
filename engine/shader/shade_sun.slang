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
[[vk::binding(46, 0)]] StructuredBuffer<float> gNoise;
[[vk::binding(47, 0)]] Texture2D<float> gContact;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

float3 shadowPoint(uint cascade, float3 P, float3 N, float NdotL) {
    const float texel = max(gSun.sunSplit[cascade], 1.0) * 2.0 / 4096.0;
    const float slope = sqrt(saturate(1.0 - NdotL * NdotL));
    return P + N * texel * slope;
}

float cloudNoiseAt(float3 uv, uint res, uint base) {
    const float3 q = frac(uv) * float(res);
    const uint3 i = uint3(q) % res;
    return gNoise[base + i.x + i.y * res + i.z * res * res];
}

float cloudDensityAt(float3 world, float height01) {
    const float3 wind = float3(gAtm.cloudSpeedX, height01 * 8.0, gAtm.cloudSpeedZ) * gAtm.time;
    const float3 p = world + wind;
    const float worley = cloudNoiseAt(p / 1800.0, 128u, 0u);
    const float shape = saturate((0.62 - worley) * 2.4);
    const float detail = cloudNoiseAt(p / 320.0 + 19.0, 32u, 128u * 128u * 128u);
    const float base = shape * gAtm.cloudCoverage * 1.6 - detail * gAtm.cloudDetail * (0.2 + height01);
    return saturate(base) * gAtm.cloudDensity * 14.0;
}

// Тот же слой, что рисует cloudMarch: луч солнца через низ и верх облаков.
float cloudGroundShadow(float3 worldPos, float3 L) {
    if ((gPass.passMask & 2048u) == 0u || L.y <= 0.05) {
        return 1.0;
    }
    if (gAtm.cloudCoverage <= 0.01 || gAtm.cloudDensity <= 0.0005) {
        return 1.0;
    }
    const float bottom = gAtm.cloudBottom;
    const float top = max(gAtm.cloudTop, bottom + 200.0);
    const float span = top - bottom;
    const float dt = span / 6.0;
    float optical = 0.0;
    [unroll]
    for (int i = 0; i < 6; ++i) {
        const float u = (float(i) + 0.5) / 6.0;
        const float h = bottom + span * u;
        const float t = (h - worldPos.y) / L.y;
        if (t <= 0.0) {
            continue;
        }
        optical += cloudDensityAt(worldPos + L * t, u) * dt * 0.012;
    }
    const float trans = exp(-optical);
    return lerp(0.42, 1.0, trans);
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
        const float o2 = cascadeOutside(2u, p2);

        const float s2 = o2 <= 0.0 ? cascadeShadow(2u, p2) : cascadeShadowCover(2u, p2);
        const float s1 = o1 <= 0.0 ? min(cascadeShadow(1u, p1), s2) : s2;

        if (o0 <= 0.0) {
            const float w = saturate(-o0 / 0.12);
            const float s0 = min(cascadeShadow(0u, p0), s1);
            shadow = lerp(s1, s0, w);
            sunCascade = w > 0.5 ? 0u : 1u;
        } else if (o1 <= 0.0) {
            const float w = saturate(-o1 / 0.12);
            shadow = lerp(s2, s1, w);
            sunCascade = w > 0.5 ? 1u : 2u;
        } else {
            shadow = s2;
            sunCascade = 2u;
        }
    }
    if ((gPass.passMask & 16384u) != 0u && gShade.sscsAmt > 0.001 && hit.NdotL > 0.05) {
        const float crease = gContact.Load(int3(px, 0));
        if (crease == crease && crease > 0.02) {
            shadow *= saturate(crease);
        }
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
    shadow *= cloudGroundShadow(hit.P, hit.L);
    float3 sunCol = gSun.sunColor.xyz;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.97, 0.92);
    }
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    float3 radiance = sunCol * inten;
    const bool sunOn = (gPass.passMask & 256u) != 0u;
    if (!sunOn) {
        radiance = 0.0;
    }
    const bool giOn = (gPass.passMask & 262144u) != 0u;
    const bool worldRc = giOn && (gPass.passMask & 128u) != 0u;
    const bool voxelGi = giOn && (gPass.passMask & 524288u) != 0u;
    const float3 multiAo = gtaoMultiBounce(hit.gtao, hit.albedo);
    float3 bounce = giOn ? shAmbient(hit.N, hit.P) * 0.4 * multiAo : 0.0;
    if (worldRc) {
        bounce = 0.0;
    }
    if (!sunOn) {
        bounce *= 0.02;
    }
    if (giOn && !worldRc && !voxelGi && gFrame.disableShadow == 0 && shadow < 0.95) {
        const float reach = max(shadowSkyReach(sunCascade, hit.P, hit.N, px), 0.15);
        const float skyVis = lerp(reach, 1.0, saturate(shadow));
        bounce *= skyVis;
    }
    const float giGain = giOn ? max(gShade.pad1, 0.0) : 0.0;
    bounce *= giGain;
    const float3 bounceLit = hit.albedo * (1.0 - hit.metallic) * bounce * sunCol * saturate(inten / 8.0);
    float3 skyAmb = 0.0;
    if (!worldRc) {
        const float3 skyDir = normalize(float3(hit.N.x, hit.N.y + 1.15, hit.N.z));
        const float3 skyLit = max(gSkyView.SampleLevel(gClamp, atmSkyUv(skyDir), 0.0).rgb, 0.0);
        const float skyLum = dot(skyLit, float3(0.2126, 0.7152, 0.0722));
        const float3 skyChroma = skyLum > 0.05 ? skyLit / skyLum : float3(0.45, 0.62, 1.0);
        const float wrap = saturate(hit.N.y * 0.4 + 0.6);
        const float ambOcc = lerp(0.55, 1.0, saturate(hit.gtao));
        skyAmb = hit.albedo * (1.0 - hit.metallic) * skyChroma * (inten * 0.16) * wrap * ambOcc;
        if (!sunOn) {
            skyAmb *= 0.15;
        }
    }
    float3 indirect = bounceLit + skyAmb + hit.emissive;
    float sunFace = hit.NdotL;
    if (hit.foliage > 0.5) {
        sunFace = max(sunFace, saturate(dot(-hit.N, hit.L)) * 0.45);
    }
    float3 direct = (hit.diff + hit.spec) * sunFace * shadow * radiance;
    if (!all(direct == direct)) {
        direct = 0.0;
    }
    if (gFrame.debugMode == 8u) {
        gHdr[px] = float4(direct / max(inten, 1.0), 1.0);
        return;
    }
    if (gFrame.debugMode == 9u) {
        gHdr[px] = float4(hit.albedo, 1.0);
        return;
    }
    if (gFrame.debugMode == 10u) {
        gHdr[px] = float4(hit.albedo * hit.NdotL * shadow, 1.0);
        return;
    }
    if (gFrame.debugMode == 11u) {
        gHdr[px] = float4(hit.roughness, hit.roughness, hit.roughness, 1.0);
        return;
    }
    if (gFrame.debugMode == 12u) {
        gHdr[px] = float4(hit.metallic, hit.metallic, hit.metallic, 1.0);
        return;
    }
    if (gFrame.debugMode == 13u) {
        gHdr[px] = float4(hit.ao, hit.ao, hit.ao, 1.0);
        return;
    }
    if (gFrame.debugMode == 14u) {
        gHdr[px] = float4(hit.emissive, 1.0);
        return;
    }
    if (gFrame.debugMode >= 15u && gFrame.debugMode <= 19u) {
        gHdr[px] = float4(0.15, 0.25, 0.85, 1.0);
        return;
    }
    if (gPass.lightProfile == 3u && (gPass.passMask & 2097152u) != 0u) {
        direct = 0.0;
    }
    float3 color = indirect + direct;
    if (!worldRc && !voxelGi && (gPass.passMask & 8192u) != 0u && gPass.lightProfile < 1u) {
        const float gloss = saturate(1.0 - hit.roughness);
        const float2 dfg = dfgEnv(hit.NdotV, hit.roughness);
        const float3 envSpec = hit.f0 * dfg.x + dfg.y;
        const float specAo = specOcclusion(hit.NdotV, hit.gtao, hit.roughness);
        const float3 env = sampleEnv(boxProjectDir(hit.P, reflect(-hit.V, hit.N)), hit.roughness) * envSpec * specAo * gloss * 0.35;
        if (all(env == env)) {
            color += max(env, 0.0);
        }
        if (hit.mirror > 0.5) {
            const float3 floorR = float3(-hit.V.x, hit.V.y, -hit.V.z);
            color += sampleEnv(boxProjectDir(hit.P, floorR), hit.roughness) * envSpec * specAo * 0.35;
        }
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
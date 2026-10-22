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
[[vk::binding(36, 0)]] Texture2D<float> gCloudShadow;
[[vk::binding(47, 0)]] Texture2D<float> gContact;
[[vk::binding(58, 0)]] ConstantBuffer<AtmosphereParams> gAtm;

float3 shadowPoint(uint cascade, float3 P, float3 N, float NdotL) {
    const float texel = max(gSun.sunSplit[cascade], 1.0) * 2.0 / 4096.0;
    const float slope = sqrt(saturate(1.0 - NdotL * NdotL));
    return P + N * texel * slope;
}

// Карта 1024², слот 937. Сдвиг вдоль солнца до низа слоя, та же сетка 8 м, что пишет cloudShadowCs.
float cloudGroundShadow(float3 worldPos, float3 L) {
    if ((gPass.passMask & 2048u) == 0u || L.y <= 0.05) {
        return 1.0;
    }
    const float bottom = max(gAtm.cloudBottom, 1200.0);
    const float cell = 8.0;
    const float2 origin = floor(gFrame.cameraPos.xz / cell) * cell;
    const float t = clamp((bottom - worldPos.y) / max(L.y, 0.2), 0.0, 12000.0);
    const float2 xz = worldPos.xz + L.xz * t;
    const float2 uv = (xz - origin) / 8192.0 + 0.5;
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
        return 1.0;
    }
    const float vis = gCloudShadow.Load(int3(int2(uv * 1024.0), 0));
    return lerp(0.45, 1.0, saturate(vis));
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
    if ((gPass.passMask & 2097152u) != 0u && (gFrame.debugMode == 0u || gFrame.debugMode == 4u)) {
        float3 skyAmb = 0.0;
        if ((gPass.passMask & 1024u) != 0u) {
            const float inten = gSun.sunDirIntensity.w;
            const float wrap = saturate(hit.N.y * 0.4 + 0.6);
            const float3 skyChroma = lerp(float3(0.78, 0.82, 0.88), float3(0.50, 0.54, 0.60), saturate(gAtm.cloudCoverage));
            skyAmb = hit.albedo * (1.0 - hit.metallic) * skyChroma * (inten * 0.055) * wrap * saturate(hit.gtao);
        }
        gHdr[px] = float4(max(skyAmb + hit.emissive, 0.0), hit.roughness);
        return;
    }
    uint sunCascade = 2u;
    float shadow = 1.0;
    if (gFrame.disableShadow == 0 && gFrame.debugPad0 == 0u && (gPass.passMask & 2097152u) == 0u) {
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
    if (gFrame.disableShadow != 0 || gFrame.debugPad0 != 0u) {
        shadow = 1.0;
    }
    shadow *= cloudGroundShadow(hit.P, hit.L);
    float3 sunCol = gSun.sunColor.xyz;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.92, 0.76);
    }
    const float inten = gSun.sunDirIntensity.w;
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
        bounce = 0.0;
    }
    if (giOn && !worldRc && !voxelGi && gFrame.disableShadow == 0 && shadow < 0.95) {
        const float reach = max(shadowSkyReach(sunCascade, hit.P, hit.N, px), 0.15);
        const float skyVis = lerp(reach, 1.0, saturate(shadow));
        bounce *= skyVis;
    }
    const float giGain = giOn ? max(gShade.pad1, 0.0) : 0.0;
    bounce *= giGain;
    float3 bounceLit = hit.albedo * (1.0 - hit.metallic) * bounce * sunCol * saturate(inten / 8.0) * max(asfloat(gFrame.debugPad1), 0.0);
    float3 skyAmb = 0.0;
    if (!worldRc) {
        const float3 skyDir = normalize(float3(hit.N.x, hit.N.y + 1.15, hit.N.z));
        const float3 skyLit = max(gSkyView.SampleLevel(gClamp, atmSkyUv(skyDir), 0.0).rgb, 0.0);
        const float skyLum = dot(skyLit, float3(0.2126, 0.7152, 0.0722));
        const float3 skyNeutral = float3(0.78, 0.82, 0.88);
        const float3 skyChroma = skyLum > 0.05 ? skyLit / skyLum : skyNeutral;
        const float3 skyTint = lerp(skyNeutral, skyChroma, 0.35);
        const float wrap = saturate(hit.N.y * 0.4 + 0.6);
        const float ambOcc = saturate(hit.gtao);
        skyAmb = hit.albedo * (1.0 - hit.metallic) * skyTint * (inten * 0.055) * wrap * ambOcc;
        if ((gPass.passMask & 1024u) == 0u) {
            skyAmb = 0.0;
        }
    }
    if (giOn && (gPass.passMask & 8388608u) != 0u) {
        bounceLit = 0.0;
    }
    float3 indirect = bounceLit + skyAmb + hit.emissive;
    float sunFace = hit.NdotL;
    if (hit.foliage > 0.5) {
        sunFace = max(sunFace, saturate(dot(-hit.N, hit.L)) * 0.45);
    }
    const float3 sunDiff = coloredPenumbra(hit.diff, hit.albedo, shadow);
    float3 direct = ((sunDiff + hit.spec) * sunFace + hit.coat * hit.coatNoL) * shadow * radiance;
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
    if ((gPass.passMask & 2097152u) != 0u) {
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
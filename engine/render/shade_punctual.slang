// Лампы добавляют свет в HdrA. Поверхность уже лежит в gSurf0 и gSurf1.

#include "frame.slang"
#include "camera.slang"
#include "shade_tile.slang"
#include "core/brdf.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(13, 0)]] SamplerState gSamp;
[[vk::binding(17, 0)]] Texture2D<float> gPointShadow;
[[vk::binding(25, 0)]] StructuredBuffer<LightGpu> gLights;
[[vk::binding(26, 0)]] StructuredBuffer<uint4> gCells;
[[vk::binding(27, 0)]] StructuredBuffer<uint> gLightIds;
[[vk::binding(32, 0)]] Texture2D<float> gAo;

struct LightGpu {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

[shader("compute")]
[numthreads(16, 16, 1)]
void shadePunctual(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex) {
    if (gFrame.debugMode == 1u || gFrame.debugMode == 2u || gFrame.debugMode == 5u) {
        return;
    }
    uint vis;
    uint2 px;
    if (!shadeTile(dtid, gtid, vis, px)) {
        return;
    }
    if ((gPass.passMask & 1u) == 0u) {
        return;
    }
    const float4 s0 = gSurf0[px];
    const float4 s1 = gSurf1[px];
    const float3 albedo = s0.rgb;
    const float roughness = s0.w;
    const float3 N = octDecode(s1.xy);
    const float metallic = s1.z;
    const float ormAo = s1.w;
    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(px, depth);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    const float NdotV = saturate(dot(N, V));
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float aoTex = gAo.Load(int3(px, 0)).r;
    const float gtaoAmt = max(gShade.gtaoAmt, 0.0);
    const float gtao = (gtaoAmt > 0.001 ? pow(max(aoTex, 0.02), gtaoAmt) : 1.0) * ormAo;
    const float a = max(roughness * roughness, 0.002);
    float3 color = gHdr[px].rgb;
    const float4 clipP = worldToClip(P);
    const float2 ndcP = clipP.xy / max(clipP.w, 1e-4) * 0.5 + 0.5;
    const float nearZ = max(gFrame.cameraPos.w, 0.05);
    const float viewZ = max(clipP.w, nearZ);
    const float zFar = max(max(gSun.sunSplit.z, viewZ), nearZ * 1.01);
    const float zNorm = saturate(log(viewZ / nearZ) / log(zFar / nearZ));
    const uint cx = min(uint(saturate(ndcP.x) * 16.0), 15u);
    const uint cy = min(uint(saturate(ndcP.y) * 9.0), 8u);
    const uint cz = min(uint(zNorm * 24.0), 23u);
    const uint4 cell = gCells[cx + cy * 16u + cz * 144u];
    bool lampContact = false;
    for (uint n = 0; n < cell.y && n < 64u; ++n) {
        const LightGpu lamp = gLights[gLightIds[cell.x + n]];
        const uint kind = lamp.meta.x;
        if (kind == 0) {
            continue;
        }
        float3 Lp = lamp.dirAngle.xyz;
        float atten = 1.0;
        float dist = 1e9;
        int shadowSlot = -1;
        const float3 toLight = lamp.posRadius.xyz - P;
        dist = length(toLight);
        const float radius = lamp.posRadius.w;
        if (radius <= 0.0 || dist >= radius || dist <= 1e-4) {
            continue;
        }
        Lp = toLight / dist;
        atten = saturate(1.0 - dist / radius);
        atten *= atten;
        if (kind == 2 && dot(Lp, -normalize(lamp.dirAngle.xyz)) < cos(max(lamp.dirAngle.w, 0.05))) {
            continue;
        }
        shadowSlot = int(lamp.meta.z);
        float pshadow = 1.0;
        if (gFrame.disableShadow == 0 && shadowSlot >= 0 && shadowSlot < 3) {
            const float3 toPoint = -Lp;
            uint face = 0;
            float2 faceUv = 0.0;
            cubeDirToFaceUV(toPoint, face, faceUv);
            const float4 pclip = mul(gSun.pointFace[shadowSlot][face], float4(P, 1.0));
            const float pw = pclip.w == 0.0 ? 1.0 : pclip.w;
            const float2 pLocal = pclip.xy / pw * 0.5 + 0.5;
            if (pLocal.x > 0.0 && pLocal.y > 0.0 && pLocal.x < 1.0 && pLocal.y < 1.0) {
                const float2 pUv = float2((pLocal.x + float(face)) / 6.0, (pLocal.y + float(shadowSlot)) / 3.0);
                const float closest = gPointShadow.SampleLevel(gSamp, pUv, 0.0);
                const float receiverZ = pclip.z / pw;
                pshadow = closest >= 0.998 ? 1.0 : receiverZ <= closest + 0.012 ? 1.0 : 0.0;
            }
        }
        const float ndl = saturate(dot(N, Lp));
        const float3 H2 = normalize(V + Lp);
        const float3 F2 = F_Schlick(saturate(dot(V, H2)), f0);
        const float D2 = D_GGX(saturate(dot(N, H2)), a);
        const float G2 = G_Smith(NdotV, ndl, a);
        float3 specL = min((D2 * G2 * F2) / max(4.0 * NdotV * ndl, 1e-4), float3(2.0, 2.0, 2.0));
        specL *= lerp(float3(1.0, 1.0, 1.0), energyCompensation(f0, NdotV, roughness), saturate(gShade.ggx));
        specL *= lerp(1.0, specOcclusion(NdotV, gtao, roughness), saturate(gShade.specOcc));
        const float3 diffL = albedo * (1.0 / kPi) * (1.0 - F2) * (1.0 - metallic);
        float3 lampRad = lamp.colorIntensity.rgb * lamp.colorIntensity.w;
        const float lampPeak = max(lampRad.r, max(lampRad.g, lampRad.b));
        const float lampCap = lerp(1.2, 8.0, saturate(gShade.lamp));
        if (lampPeak > lampCap) {
            lampRad *= lampCap / lampPeak;
        }
        const float core = saturate(1.0 - dist / 0.35) * saturate(gShade.lamp);
        color += lamp.colorIntensity.rgb * core * core * 8.0;
        color += (diffL * ormAo + specL) * ndl * atten * lampRad * pshadow;
        if (!lampContact && dist < 6.0 && ndl > 0.05) {
            lampContact = true;
            const float4 aClip = worldToClip(P);
            const float4 bClip = worldToClip(P + Lp * 0.35);
            if (aClip.w > 1e-3 && bClip.w > 1e-3) {
                const float2 px0 = (aClip.xy / aClip.w * 0.5 + 0.5) * float2(gFrame.extent.xy);
                const float2 px1 = (bClip.xy / bClip.w * 0.5 + 0.5) * float2(gFrame.extent.xy);
                const float stepsF = clamp(length(px1 - px0), 3.0, 10.0);
                const float2 stepPx = (px1 - px0) / stepsF;
                [loop]
                for (int s = 1; s <= int(stepsF); ++s) {
                    const float2 sp = px0 + stepPx * float(s);
                    if (sp.x <= 0.0 || sp.y <= 0.0 || sp.x >= gFrame.extent.x || sp.y >= gFrame.extent.y) {
                        break;
                    }
                    const float sd = gDepth.Load(int3(int2(sp), 0));
                    if (sd <= 0.001) {
                        continue;
                    }
                    const float3 sceneP = reconstructWorld(uint2(sp), sd);
                    const float along = dot(sceneP - P, Lp);
                    if (gShade.sscsAmt > 0.5 && along > 0.04 && along < 0.3 && length(sceneP - P - Lp * along) < 0.02) {
                        color *= 0.72;
                        break;
                    }
                }
            }
        }
    }
    gHdr[px] = float4(color, gHdr[px].a);
}

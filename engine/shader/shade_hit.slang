// Поверхность из visbuffer. Солнце зовёт shadeHit и кладёт её в две картинки. Лампы их читают.

#include "shade_tile.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(18, 0)]] StructuredBuffer<DecalGpu> gDecals;
[[vk::binding(19, 0)]] StructuredBuffer<ProbeGpu> gProbes;
[[vk::binding(26, 0)]] StructuredBuffer<uint4> gCells;
[[vk::binding(27, 0)]] StructuredBuffer<uint> gLightIds;
[[vk::binding(28, 0)]] StructuredBuffer<uint> gPages;
[[vk::binding(29, 0)]] RWStructuredBuffer<uint> gFeedback;
[[vk::binding(32, 0)]] Texture2D<float> gAo;

#include "core/brdf.slang"
#include "core/vis_surface.slang"

struct DecalGpu {
    float4 centerRadius;
    float4 axisXOpacity;
    float4 axisYTex;
    float4 axisZPad;
    float4 color;
};

struct ProbeGpu {
    float4 band0;
    float4 band1;
    float4 band2;
    float4 band3;
    float4 band4;
    float4 band5;
    float4 band6;
    float4 band7;
};

struct ShadeHit {
    uint tri;
    float3 N;
    float3 P;
    float3 V;
    float3 L;
    float3 albedo;
    float3 spec;
    float3 diff;
    float3 f0;
    float3 emissive;
    float roughness;
    float metallic;
    float ao;
    float gtao;
    float NdotL;
    float NdotV;
    float pomShadow;
    float mirror;
    float foliage;
    float3 coat;
    float coatNoL;
};

float4 sampleHeap(uint id, float2 uv, float lod) {
    return gTex[NonUniformResourceIndex(id)].SampleLevel(gWrap, uv, lod);
}

float4 sampleGrad(uint id, float2 uv, float2 ddx, float2 ddy) {
    return gTex[NonUniformResourceIndex(id)].SampleGrad(gWrap, uv, ddx, ddy);
}

float2 parallaxOcclusionUv(float2 uv, float2 uvDx, float2 uvDy, float3 viewTS, float ndotv, uint htex, float amount) {
    const int nNumSteps = clamp(int(lerp(48.0, 8.0, saturate(ndotv))), 8, 48);
    const float fStepSize = 1.0 / float(nNumSteps);
    const float heightScale = 0.05 * amount / 0.35;
    const float2 vParallaxOffsetTS = viewTS.xy * heightScale / max(viewTS.z, 0.05);
    const float2 vTexOffsetPerStep = fStepSize * vParallaxOffsetTS;
    float2 vTexCurrentOffset = uv;
    float fPrevHeight = 1.0;
    float fCurrentBound = 1.0;
    float2 pt1 = 0.0;
    float2 pt2 = 0.0;
    bool hitLayer = false;
    [loop]
    for (int nStepIndex = 0; nStepIndex < nNumSteps; ++nStepIndex) {
        vTexCurrentOffset -= vTexOffsetPerStep;
        const float fCurrHeight = sampleGrad(htex, vTexCurrentOffset, uvDx, uvDy).r;
        fCurrentBound -= fStepSize;
        if (fCurrHeight > fCurrentBound) {
            pt1 = float2(fCurrentBound, fCurrHeight);
            pt2 = float2(fCurrentBound + fStepSize, fPrevHeight);
            hitLayer = true;
            break;
        }
        fPrevHeight = fCurrHeight;
    }
    float fParallaxAmount = 0.0;
    if (hitLayer) {
        const float fDelta2 = pt2.x - pt2.y;
        const float fDelta1 = pt1.x - pt1.y;
        const float fDenominator = fDelta2 - fDelta1;
        if (abs(fDenominator) > 1e-5) {
            fParallaxAmount = (pt1.x * fDelta2 - pt2.x * fDelta1) / fDenominator;
        }
    }
    return uv - vParallaxOffsetTS * (1.0 - fParallaxAmount);
}

float shCoeff(ProbeGpu pr, uint i) {
    const float4 row = i < 4u ? pr.band0 : i < 8u ? pr.band1 : i < 12u ? pr.band2 : i < 16u ? pr.band3 : i < 20u ? pr.band4 : i < 24u ? pr.band5 : i < 28u ? pr.band6 : pr.band7;
    const uint e = i & 3u;
    return e == 0u ? row.x : e == 1u ? row.y : e == 2u ? row.z : row.w;
}

float3 shFromProbe(ProbeGpu pr, float3 N) {
    const float b0 = 0.2820948;
    const float b1 = 0.4886025;
    const float b2 = 1.0925484;
    const float b3 = 0.3153916;
    const float b4 = 0.5462742;
    const float basis[9] = {
        b0, b1 * N.y, b1 * N.z, b1 * N.x,
        b2 * N.x * N.y, b2 * N.y * N.z, b3 * (3.0 * N.z * N.z - 1.0), b2 * N.x * N.z, b4 * (N.x * N.x - N.y * N.y)
    };
    float3 rgb = 0.0;
    [unroll]
    for (uint band = 0u; band < 9u; ++band) {
        const uint i = band * 3u;
        rgb += float3(shCoeff(pr, i), shCoeff(pr, i + 1u), shCoeff(pr, i + 2u)) * basis[band];
    }
    return rgb;
}

float3 shAmbient(float3 N, float3 P) {
    if ((gPass.passMask & 2u) == 0u) {
        return float3(0.16, 0.16, 0.16);
    }
    const uint n = gFrame.debugPad1;
    if (n == 0u) {
        return float3(0.16, 0.16, 0.16);
    }
    const float3 mn = gSun.cubeMin.xyz;
    const float3 mx = max(gSun.cubeMax.xyz, mn + float3(1.0, 1.0, 1.0));
    const float3 f = saturate((P - mn) / (mx - mn)) * float(n - 1u);
    if (n < 2u) {
        return max(shFromProbe(gProbes[0], N), 0.0);
    }
    const uint3 i0 = min(uint3(floor(f)), uint3(n - 2u, n - 2u, n - 2u));
    const float3 t = saturate(f - float3(i0));
    float3 acc = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const uint3 cell = i0 + uint3(corner & 1u, (corner >> 1u) & 1u, corner >> 2u);
        const float wx = cell.x == i0.x ? 1.0 - t.x : t.x;
        const float wy = cell.y == i0.y ? 1.0 - t.y : t.y;
        const float wz = cell.z == i0.z ? 1.0 - t.z : t.z;
        acc += shFromProbe(gProbes[cell.x + cell.y * n + cell.z * n * n], N) * (wx * wy * wz);
    }
    return max(acc, 0.0);
}

ShadeHit shadeHit(uint2 px, uint vis, uint gtid, bool feedback) {
    ShadeHit hit;
    hit.pomShadow = 1.0;
    hit.mirror = 0.0;
    hit.foliage = 0.0;
    hit.coat = 0.0;
    hit.coatNoL = 0.0;
    const uint packed = vis - 1u;
    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(px, depth);
    const VisTri tri = loadVisTri(packed & 0xFFFFF, packed >> 20, P);
    hit.tri = tri.triId;
    const GpuInstance inst = tri.inst;
    const uint i0 = tri.i0;
    const uint i1 = tri.i1;
    const uint i2 = tri.i2;
    const float3 w0 = tri.w0;
    const float3 w1 = tri.w1;
    const float3 w2 = tri.w2;
    float3 N = tri.faceN;
    const float3 n0 = tri.n0;
    const float3 n1 = tri.n1;
    const float3 n2 = tri.n2;
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 e1n = w1 - w0;
    const float3 e2n = w2 - w0;
    const float faceA = tri.faceA;
    const float buC = tri.denomOk ? tri.bu : 1.0;
    const float bvC = tri.denomOk ? tri.bv : 0.0;
    const float bwC = tri.denomOk ? tri.bw : 0.0;
    const float2 uv0e = float2(gVerts[i0].u, gVerts[i0].v);
    const float2 uv1e = float2(gVerts[i1].u, gVerts[i1].v);
    const float2 uv2e = float2(gVerts[i2].u, gVerts[i2].v);
    const float uvPerM0 = max(length(uv1e - uv0e), length(uv2e - uv0e)) / max(max(length(e1n), length(e2n)), 1e-4);
    const float mPerPx = max(length(gFrame.cameraPos.xyz - P), 0.2) * gFrame.invExtent.y * 2.0;
    const float grad = clamp(uvPerM0 * mPerPx, 1e-5, 0.02);
    float2 uvDdx = float2(grad, 0.0);
    float2 uvDdy = float2(0.0, grad);
    const MaterialGpu mat = gMat[inst.materialId];
    float3 tangent = 0.0;
    float3 bitangent = 0.0;
    float normalLen = 1.0;
    if (faceA > 1e-6) {
        float3 Ns = n0 * buC + n1 * bvC + n2 * bwC;
        if (dot(Ns, Ns) > 1e-6) {
            Ns = normalize(Ns);
            if (dot(Ns, N) < 0.0) {
                Ns = -Ns;
            }
            N = Ns;
        }
        const float2 uv0 = float2(gVerts[i0].u, gVerts[i0].v);
        const float2 uv1 = float2(gVerts[i1].u, gVerts[i1].v);
        const float2 uv2 = float2(gVerts[i2].u, gVerts[i2].v);
        const float2 duv1 = uv1 - uv0;
        const float2 duv2 = uv2 - uv0;
        const float det = duv1.x * duv2.y - duv2.x * duv1.y;
        if (abs(det) > 1e-8) {
            const float invDet = 1.0 / det;
            float3 T = (e1n * duv2.y - e2n * duv1.y) * invDet;
            T = T - N * dot(N, T);
            if (dot(T, T) > 1e-8) {
                T = normalize(T);
                tangent = T;
                bitangent = cross(N, T);
            }
        }
    }
    float3 albedo = mat.baseColor.rgb;
    float2 uv = float2(
        gVerts[i0].u * buC + gVerts[i1].u * bvC + gVerts[i2].u * bwC,
        gVerts[i0].v * buC + gVerts[i1].v * bvC + gVerts[i2].v * bwC);
    const uint htex = mat.pad0;
    if (gShade.pom > 0.001 && htex > 0u && htex < 768u && dot(tangent, tangent) > 0.5) {
        const float3 viewTS = float3(dot(V, tangent), dot(V, bitangent), dot(V, N));
        uv = parallaxOcclusionUv(uv, uvDdx, uvDdy, viewTS, saturate(dot(V, N)), htex, saturate(gShade.pom));
    }
    const float3 geoN = dot(N, V) < 0.0 ? -N : N;
    const uint ntex = mat.normalTex;
    if ((mat.flags & 32u) == 0u && ntex > 0u && ntex < 768u && dot(tangent, tangent) > 0.5) {
        float3 tN = sampleGrad(ntex, uv, uvDdx, uvDdy).xyz * 2.0 - 1.0;
        tN.y = -tN.y;
        if (dot(tN.xy, tN.xy) > 1e-4 && tN.z < 0.05) {
            tN.z = sqrt(max(1.0 - dot(tN.xy, tN.xy), 0.0));
        }
        normalLen = saturate(length(tN.xy));
        N = normalize(tangent * tN.x + bitangent * tN.y + N * tN.z);
    }
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    const float3 L = dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
    const float3 H = normalize(V + L);
    const float NdotL = saturate(dot(N, L));
    const float NdotV = max(dot(N, V), 1e-4);
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));
    const uint tex = mat.albedo;
    if (tex < 768) {
        float lod = 0.0;
        if ((gPass.passMask & 32768u) != 0u) {
            const uint page = min(uint(saturate(uv.x) * 8.0) + uint(saturate(uv.y) * 8.0) * 8u, 63u);
            if (gPages[page] == 0u) {
                lod = 4.0;
            }
        }
        const float4 texel = sampleGrad(tex, uv, uvDdx, uvDdy);
        albedo *= texel.rgb;
        if ((mat.flags & 32u) != 0u && texel.a < 0.98) {
            hit.foliage = 1.0;
        }
        if (lod > 0.0) {
            albedo = sampleHeap(tex, uv, lod).rgb;
        }
        if (feedback && gtid == 0u) {
            const uint vpage = min(uint(saturate(uv.x) * 8.0) + uint(saturate(uv.y) * 8.0) * 8u, 63u);
            uint slot = 0u;
            InterlockedAdd(gFeedback[0], 1u, slot);
            if (slot < 63u) {
                gFeedback[1u + slot] = vpage;
            }
        }
    }
    const float4 clipD = worldToClip(P);
    const float2 ndcD = clipD.xy / max(clipD.w, 1e-4) * 0.5 + 0.5;
    const float nearD = max(gFrame.cameraPos.w, 0.05);
    const float viewD = max(clipD.w, nearD);
    const float farD = max(max(gSun.sunSplit.z, viewD), nearD * 1.01);
    const float zD = saturate(log(viewD / nearD) / log(farD / nearD));
    const uint4 decalCell = gCells[
        min(uint(saturate(ndcD.x) * 16.0), 15u)
        + min(uint(saturate(ndcD.y) * 9.0), 8u) * 16u
        + min(uint(zD * 24.0), 23u) * 144u];
    for (uint d = 0u; d < decalCell.w && d < 8u; ++d) {
        const DecalGpu decal = gDecals[gLightIds[decalCell.z + d]];
        const float3 delta = P - decal.centerRadius.xyz;
        const float xx = max(dot(decal.axisXOpacity.xyz, decal.axisXOpacity.xyz), 1e-6);
        const float yy = max(dot(decal.axisYTex.xyz, decal.axisYTex.xyz), 1e-6);
        const float zz = max(dot(decal.axisZPad.xyz, decal.axisZPad.xyz), 1e-6);
        const float lx = dot(delta, decal.axisXOpacity.xyz) / xx;
        const float ly = dot(delta, decal.axisYTex.xyz) / yy;
        const float lz = dot(delta, decal.axisZPad.xyz) / zz;
        if (abs(lx) < 1.0 && abs(ly) < 1.0 && abs(lz) < 1.0) {
            float3 stamp = decal.color.rgb;
            const uint dtex = asuint(decal.axisYTex.w);
            if (dtex < 768u) {
                stamp *= sampleHeap(dtex, float2(lx, ly) * 0.5 + 0.5, 0.0).rgb;
            }
            albedo = lerp(albedo, stamp, saturate(decal.axisXOpacity.w) * saturate(decal.color.a));
        }
    }
    float ao = 1.0;
    float metallic = saturate(mat.metallic);
    float roughness = clamp(mat.roughness, 0.045, 1.0);
    if (mat.orm > 0 && mat.orm < 768) {
        const float3 orm = sampleGrad(mat.orm, uv, uvDdx, uvDdy).rgb;
        if ((mat.flags & 4u) != 0u) {
            ao = orm.r > 0.02 ? saturate(orm.r) : 1.0;
            roughness = clamp(orm.g, 0.045, 1.0);
            metallic = saturate(orm.b);
        } else {
            const float peak = max(orm.r, max(orm.g, orm.b));
            roughness = lerp(0.82, 0.22, saturate(peak));
            metallic = peak > 0.92 ? saturate((peak - 0.92) / 0.08) : 0.0;
        }
    }
    const float normalGap = saturate(1.0 - normalLen);
    const float kernelRough = min(2.0 * normalGap * normalGap * max(gShade.toksvig, 0.0), 0.18);
    roughness = sqrt(saturate(roughness * roughness + kernelRough));
    const float coat = (mat.flags & 64u) != 0u ? 1.0 : 0.0;
    const float coatPerceptual = 0.045;
    if (coat > 0.0) {
        roughness = lerp(roughness, max(roughness, coatPerceptual), coat);
    }
    roughness = clamp(roughness, 0.045, 1.0);
    const float a = roughness * roughness;
    float reflectance = asfloat(mat.pad1);
    if (reflectance < 0.01 || reflectance > 1.0) {
        reflectance = 0.5;
    }
    float3 f0 = computeF0(albedo, metallic, computeDielectricF0(reflectance));
    if (coat > 0.0) {
        f0 = lerp(f0, f0ClearCoatToSurface(f0), coat);
    }
    const float3 F = F_Schlick(VdotH, f0);
    const float D = D_GGX(NdotH, a);
    const float Vis = V_SmithGGXCorrelated(NdotV, NdotL, a);

    float aoTex = 1.0;
    if ((gPass.passMask & 16u) != 0u && gShade.gtaoAmt > 0.001) {
        aoTex = gAo.Load(int3(px, 0)).r;
    }
    const float gtao = lerp(1.0, saturate(aoTex), saturate(gShade.gtaoAmt)) * ao;

    float3 spec = D * Vis * F;
    spec *= energyCompensation(f0, NdotV, roughness);
    spec *= lerp(1.0, specOcclusion(NdotV, gtao, roughness), saturate(gShade.specOcc));

    const float coatNoH = saturate(dot(geoN, H));
    float fCoat = 0.0;
    const float coatSpec = clearcoatLobe(coatNoH, VdotH, coat, coatPerceptual, fCoat);
    spec *= (1.0 - fCoat);

    hit.N = N;
    hit.P = P;
    hit.V = V;
    hit.L = L;
    hit.albedo = albedo;
    hit.spec = spec;
    hit.coat = float3(coatSpec, coatSpec, coatSpec);
    hit.coatNoL = saturate(dot(geoN, L));
    hit.diff = computeDiffuseColor(albedo, metallic) * Fd_Lambert() * gtao * (1.0 - fCoat);
    hit.f0 = f0;
    hit.emissive = mat.baseColor.rgb * mat.emissive * 8.0;
    if (mat.emissiveTex > 0u && mat.emissiveTex < 768u) {
        hit.emissive *= sampleGrad(mat.emissiveTex, uv, uvDdx, uvDdy).rgb;
    }
    hit.roughness = roughness;
    hit.metallic = metallic;
    hit.ao = gtao;
    hit.gtao = gtao;
    hit.NdotL = NdotL;
    hit.NdotV = NdotV;
    hit.mirror = (mat.flags & 16u) != 0u ? 1.0 : 0.0;
    return hit;
}
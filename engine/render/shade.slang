// Compute shade: visbuffer + depth + meshlet SSBO → HDR.

#include "frame.slang"
#include "camera.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(13, 0)]] SamplerState gSamp;
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(15, 0)]] Texture2D<float> gShadow0;
[[vk::binding(16, 0)]] Texture2D<float> gShadow1;
[[vk::binding(17, 0)]] Texture2D<float> gPointShadow;

struct LightGpu {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

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

float3 shAmbient(float3 N, float3 P) {
    if ((gPass.passMask & 2u) == 0u) {
        return float3(0.16, 0.16, 0.16);
    }
    const uint n = gFrame.debugPad1;
    if (n == 0u) {
        return float3(0.16, 0.16, 0.16);
    }
    const float3 mn = gFrame.cubeMin.xyz;
    const float3 mx = max(gFrame.cubeMax.xyz, mn + float3(1.0, 1.0, 1.0));
    const float3 uvw = saturate((P - mn) / (mx - mn));
    const uint3 cell = min(uint3(uvw * float(n)), uint3(n - 1u, n - 1u, n - 1u));
    const ProbeGpu pr = gProbes[cell.x + cell.y * n + cell.z * n * n];
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
        const float4 row0 = i < 4u ? pr.band0 : i < 8u ? pr.band1 : i < 12u ? pr.band2 : i < 16u ? pr.band3 : i < 20u ? pr.band4 : i < 24u ? pr.band5 : pr.band6;
        const uint e0 = i & 3u;
        const float r = e0 == 0u ? row0.x : e0 == 1u ? row0.y : e0 == 2u ? row0.z : row0.w;
        const uint i1 = i + 1u;
        const float4 row1 = i1 < 4u ? pr.band0 : i1 < 8u ? pr.band1 : i1 < 12u ? pr.band2 : i1 < 16u ? pr.band3 : i1 < 20u ? pr.band4 : i1 < 24u ? pr.band5 : pr.band6;
        const uint e1 = i1 & 3u;
        const float g = e1 == 0u ? row1.x : e1 == 1u ? row1.y : e1 == 2u ? row1.z : row1.w;
        const uint i2 = i + 2u;
        const float4 row2 = i2 < 4u ? pr.band0 : i2 < 8u ? pr.band1 : i2 < 12u ? pr.band2 : i2 < 16u ? pr.band3 : i2 < 20u ? pr.band4 : i2 < 24u ? pr.band5 : i2 < 28u ? pr.band6 : pr.band7;
        const uint e2 = i2 & 3u;
        const float b = e2 == 0u ? row2.x : e2 == 1u ? row2.y : e2 == 2u ? row2.z : row2.w;
        rgb += float3(r, g, b) * basis[band];
    }
    return max(rgb, 0.0);
}

[[vk::binding(25, 0)]] StructuredBuffer<LightGpu> gLights;
[[vk::binding(26, 0)]] StructuredBuffer<uint4> gCells;
[[vk::binding(27, 0)]] StructuredBuffer<uint> gLightIds;
[[vk::binding(18, 0)]] StructuredBuffer<DecalGpu> gDecals;
[[vk::binding(19, 0)]] StructuredBuffer<ProbeGpu> gProbes;
[[vk::binding(28, 0)]] StructuredBuffer<uint> gTexPages;
[[vk::binding(29, 0)]] RWStructuredBuffer<uint> gFeedback;
[[vk::binding(31, 0)]] StructuredBuffer<uint> gPages;
[[vk::binding(32, 0)]] Texture2D<float> gAo;

static const float kPi = 3.14159265;

float4 sampleHeap(uint id, float2 uv, float lod) {
    return gTex[NonUniformResourceIndex(id)].SampleLevel(gWrap, uv, lod);
}

float4 sampleGrad(uint id, float2 uv, float2 ddx, float2 ddy) {
    return gTex[NonUniformResourceIndex(id)].SampleGrad(gWrap, uv, ddx, ddy);
}

float D_GGX(float NdotH, float a) {
    const float a2 = a * a;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float G_Smith(float NdotV, float NdotL, float a) {
    const float r = a + 1.0;
    const float k = (r * r) / 8.0;
    const float gv = NdotV / (NdotV * (1.0 - k) + k);
    const float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

float3 F_Schlick(float VdotH, float3 f0) {
    const float t = pow(1.0 - VdotH, 5.0);
    return f0 + (1.0 - f0) * t;
}

float specOcclusion(float NdotV, float ao, float roughness) {
    return saturate(pow(NdotV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao);
}

float2 dfgEnv(float NdotV, float roughness) {
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    const float4 r = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return float2(-1.04, 1.04) * a004 + r.zw;
}

float3 energyCompensation(float3 f0, float NdotV, float roughness) {
    const float2 dfg = dfgEnv(NdotV, roughness);
    return min(1.0 + f0 * (1.0 / max(dfg.y, 1e-3) - 1.0), float3(4.0, 4.0, 4.0));
}

float shadowDepth(float2 uv) {
    return gFrame.padExt.x == 0 ? gShadow0.SampleLevel(gSamp, uv, 0.0) : gShadow1.SampleLevel(gSamp, uv, 0.0);
}

float cascadeShadow(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    const float outside = max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
    if (outside > 0.0 && cascade < 2u) {
        return 1.0;
    }
    const float receiver = sunClip.z / sunW;
    const float extent = max(gFrame.sunSplit[cascade], 1.0);
    const float bias = 0.08 / (extent * 8.0);
    const int y0 = int(cascade) * 2048;
    const int2 basePx = int2(saturate(sunLocal) * 2048.0) + int2(0, y0);
    float litSum = 0.0;
    [unroll]
    for (int sy = -2; sy <= 2; ++sy) {
        [unroll]
        for (int sx = -2; sx <= 2; ++sx) {
            const int2 sp = int2(clamp(basePx.x + sx, 0, 2047), clamp(basePx.y + sy, y0, y0 + 2047));
            const float mapDepth = gShadow0.Load(int3(sp, 0));
            litSum += (receiver + bias) >= mapDepth ? 1.0 : 0.0;
        }
    }
    return litSum / 25.0;
}

float cascadeOutside(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = sunClip.xy / sunW * 0.5 + 0.5;
    return max(max(-sunLocal.x, sunLocal.x - 1.0), max(-sunLocal.y, sunLocal.y - 1.0));
}

float cascadeLit(uint cascade, float3 world) {
    const float4 sunClip = worldToSun(cascade, world);
    const float sunW = sunClip.w == 0.0 ? 1.0 : sunClip.w;
    const float2 sunLocal = saturate(sunClip.xy / sunW * 0.5 + 0.5);
    const float receiver = sunClip.z / sunW;
    const float bias = 0.08 / (max(gFrame.sunSplit[cascade], 1.0) * 8.0);
    const int y0 = int(cascade) * 2048;
    const int2 sp = int2(sunLocal * 2048.0) + int2(0, y0);
    const float mapDepth = gShadow0.Load(int3(clamp(sp, int2(0, y0), int2(2047, y0 + 2047)), 0));
    return (receiver + bias) >= mapDepth ? 1.0 : 0.35;
}

bool shadowPage(float2 uv) {
    const uint sx = min(uint(saturate(uv.x) * 16.0), 15u);
    const uint sy = min(uint(saturate(uv.y) * 48.0), 47u);
    return gPages[sx + sy * 16u] != 0u;
}

groupshared uint gTileLit;

[shader("compute")]
[numthreads(16, 16, 1)]
void shadeCs(uint3 dtid : SV_DispatchThreadID, uint gtid : SV_GroupIndex) {
    const bool inside = dtid.x < gFrame.extent.x && dtid.y < gFrame.extent.y;
    const uint2 px = dtid.xy;
    const uint vis = inside ? gVis.Load(int3(px, 0)) : 0u;
    if (gtid == 0u) {
        gTileLit = 0u;
    }
    GroupMemoryBarrierWithGroupSync();
    if (vis != 0u) {
        InterlockedOr(gTileLit, 1u);
    }
    GroupMemoryBarrierWithGroupSync();
    if (!inside) {
        return;
    }
    if (gTileLit == 0u || vis == 0u) {
        gHdr[px] = float4(0.07, 0.09, 0.12, 1.0);
        return;
    }

    const uint packed = vis - 1;
    const uint instanceId = packed & 0xFFFFF;
    const uint tri = packed >> 20;
    const GpuInstance inst = gInstances[instanceId];
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint at = meshlet.indexOffset + tri * 3;
    const uint i0 = meshlet.vertexOffset + gIdx[at];
    const uint i1 = meshlet.vertexOffset + gIdx[at + 1];
    const uint i2 = meshlet.vertexOffset + gIdx[at + 2];
    const float3 p0 = gVerts[i0].pos;
    const float3 p1 = gVerts[i1].pos;
    const float3 p2 = gVerts[i2].pos;
    const float4 h0 = float4(p0, 1.0);
    const float4 h1 = float4(p1, 1.0);
    const float4 h2 = float4(p2, 1.0);
    const float3 w0 = float3(dot(inst.world0, h0), dot(inst.world1, h0), dot(inst.world2, h0));
    const float3 w1 = float3(dot(inst.world0, h1), dot(inst.world1, h1), dot(inst.world2, h1));
    const float3 w2 = float3(dot(inst.world0, h2), dot(inst.world1, h2), dot(inst.world2, h2));
    const float3 face = cross(w1 - w0, w2 - w0);
    float3 N = dot(face, face) > 1e-12 ? normalize(face) : float3(0.0, 1.0, 0.0);
    const float3 n0 = float3(dot(inst.world0.xyz, gVerts[i0].nrm), dot(inst.world1.xyz, gVerts[i0].nrm), dot(inst.world2.xyz, gVerts[i0].nrm));
    const float3 n1 = float3(dot(inst.world0.xyz, gVerts[i1].nrm), dot(inst.world1.xyz, gVerts[i1].nrm), dot(inst.world2.xyz, gVerts[i1].nrm));
    const float3 n2 = float3(dot(inst.world0.xyz, gVerts[i2].nrm), dot(inst.world1.xyz, gVerts[i2].nrm), dot(inst.world2.xyz, gVerts[i2].nrm));

    const float depth = gDepth.Load(int3(px, 0));
    const float3 P = reconstructWorld(px, depth);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 e1n = w1 - w0;
    const float3 e2n = w2 - w0;
    const float3 faceN = cross(e1n, e2n);
    const float faceA = dot(faceN, faceN);
    float buC = 1.0;
    float bvC = 0.0;
    float bwC = 0.0;
    const float denomN = faceA;
    if (faceA > 1e-12) {
        buC = dot(cross(w1 - P, w2 - P), faceN) / faceA;
        bvC = dot(cross(w2 - P, w0 - P), faceN) / faceA;
        bwC = dot(cross(w0 - P, w1 - P), faceN) / faceA;
    }
    const float2 uv0e = float2(gVerts[i0].u, gVerts[i0].v);
    const float2 uv1e = float2(gVerts[i1].u, gVerts[i1].v);
    const float2 uv2e = float2(gVerts[i2].u, gVerts[i2].v);
    const float uvPerM = max(length(uv1e - uv0e), length(uv2e - uv0e)) / max(max(length(e1n), length(e2n)), 1e-4);
    const float mPerPx = max(length(gFrame.cameraPos.xyz - P), 0.2) * gFrame.invExtent.y * 2.0;
    const float grad = clamp(uvPerM * mPerPx, 1e-5, 0.02);
    float2 uvDdx = float2(grad, 0.0);
    float2 uvDdy = float2(0.0, grad);
    const MaterialGpu mat = gMat[inst.materialId];
    float3 tangent = 0.0;
    float3 bitangent = 0.0;
    float normalLen = 1.0;
    if (denomN > 1e-6) {
        float3 Ns = n0 * buC + n1 * bvC + n2 * bwC;
        if (dot(Ns, Ns) > 1e-6) {
            Ns = normalize(Ns);
            if (dot(Ns, N) < 0.0) {
                Ns = -Ns;
            }
            N = Ns;
        }
        const uint ntex = mat.normalTex;
        if (ntex > 0 && ntex < 768) {
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
                    const float3 B = cross(N, T);
                    bitangent = B;
                    const float2 nuv = uv0 * buC + uv1 * bvC + uv2 * bwC;
                    const float2 rg = sampleGrad(ntex, nuv, uvDdx, uvDdy).rg * 2.0 - 1.0;
                    normalLen = saturate(length(rg));
                    const float nz = sqrt(max(1.0 - dot(rg, rg), 0.0));
                    N = normalize(T * rg.x + B * rg.y + N * nz);
                }
            }
        }
    }

    if (dot(N, V) < 0.0) {
        N = -N;
    }
    const float3 L = normalize(gFrame.sunDirIntensity.xyz);
    const float3 H = normalize(V + L);
    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    float3 albedo = mat.baseColor.rgb;
    const float bu = buC;
    const float bv = bvC;
    const float bw = bwC;
    float2 uv = float2(
        gVerts[i0].u * bu + gVerts[i1].u * bv + gVerts[i2].u * bw,
        gVerts[i0].v * bu + gVerts[i1].v * bv + gVerts[i2].v * bw);
    float pomShadow = 1.0;
    const uint htex = mat.pad0;
    const float camDist = length(gFrame.cameraPos.xyz - P);
    if (gShade.pom > 0.001 && htex > 0u && htex < 768u && camDist < 12.0 && dot(tangent, tangent) > 0.5) {
        const int steps = 8;
        const float worldLen = max(length(w1 - w0), 1e-3);
        const float uvLen = length(float2(gVerts[i1].u - gVerts[i0].u, gVerts[i1].v - gVerts[i0].v));
        const float uvPerM = min(uvLen / worldLen, 2.0);
        const float depthM = 0.004 * saturate(gShade.pom);
        const float3 vT = float3(dot(V, tangent), dot(V, bitangent), max(dot(V, N), 0.45));
        float2 shift = (vT.xy / vT.z) * (depthM * uvPerM);
        shift = clamp(shift, float2(-0.006, -0.006), float2(0.006, 0.006));
        const float2 duv = shift / float(steps);
        float h = 1.0;
        float hPrev = 1.0;
        float2 uvPrev = uv;
        const float stepH = 1.0 / float(steps);
        [loop]
        for (int i = 0; i < steps; ++i) {
            const float sampled = sampleHeap(htex, uv, 0.0).r;
            if (sampled >= h) {
                break;
            }
            hPrev = h;
            uvPrev = uv;
            h -= stepH;
            uv -= duv;
        }
        [unroll]
        for (int r = 0; r < 4; ++r) {
            const float hm = (hPrev + h) * 0.5;
            const float2 uvm = (uvPrev + uv) * 0.5;
            if (sampleHeap(htex, uvm, 0.0).r >= hm) {
                h = hm;
                uv = uvm;
            } else {
                hPrev = hm;
                uvPrev = uvm;
            }
        }
        const float3 lT = float3(dot(L, tangent), dot(L, bitangent), dot(L, N));
        if (lT.z > 0.05) {
            float2 suv = uv;
            float sh = h;
            const int shadeSteps = max(int(lerp(4.0, 8.0, saturate(gShade.pom))), 1);
            const float2 lduv = (lT.xy / lT.z) * (depthM * uvPerM / float(shadeSteps));
            const float lstep = stepH;
            [loop]
            for (int k = 0; k < shadeSteps; ++k) {
                suv += lduv;
                sh += lstep;
                if (sampleHeap(htex, suv, 0.0).r > sh) {
                    pomShadow = lerp(1.0, 0.35, saturate(gShade.pom));
                    break;
                }
            }
        }
    }
    const uint tex = mat.albedo;
    if (tex < 768) {
        albedo *= sampleGrad(tex, uv, uvDdx, uvDdy).rgb;
        const uint vpage = min(uint(saturate(uv.x) * 8.0) + uint(saturate(uv.y) * 8.0) * 8u, 63u);
        if (gtid == 0u) {
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
    const float farD = max(max(gFrame.sunSplit.z, viewD), nearD * 1.01);
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
    float metallic = mat.metallic;
    float roughness = max(mat.roughness, 0.04);
    if (mat.orm > 0 && mat.orm < 768) {
        const float3 orm = sampleGrad(mat.orm, uv, uvDdx, uvDdy).rgb;
        if ((mat.flags & 4u) != 0u) {
            ao = orm.r > 0.02 ? saturate(orm.r) : 1.0;
            roughness = max(orm.g, 0.04);
            metallic = saturate(orm.b);
        } else {
            const float peak = max(orm.r, max(orm.g, orm.b));
            roughness = lerp(0.82, 0.22, saturate(peak));
            metallic = peak > 0.92 ? saturate((peak - 0.92) / 0.08) : 0.0;
        }
    }
    const float normalGap = saturate(1.0 - normalLen);
    roughness = min(sqrt(roughness * roughness + gShade.toksvig * normalGap * normalGap), 1.0);
    const float a = max(roughness * roughness, 0.002);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
    const float3 F = F_Schlick(VdotH, f0);
    const float D = D_GGX(NdotH, a);
    const float G = G_Smith(NdotV, NdotL, a);
    float aoTex = 1.0;
    if ((gPass.passMask & 16u) != 0u && gShade.gtaoAmt > 0.001) {
        aoTex = gAo.Load(int3(px, 0)).r;
    }
    const float gtaoAmt = max(gShade.gtaoAmt, 0.0);
    const float gtao = (gtaoAmt > 0.001 ? pow(max(aoTex, 0.02), gtaoAmt) : 1.0) * ao;
    float3 spec = min((D * G * F) / max(4.0 * NdotV * NdotL, 1e-4), float3(2.0, 2.0, 2.0));
    spec *= lerp(float3(1.0, 1.0, 1.0), energyCompensation(f0, NdotV, roughness), saturate(gShade.ggx));
    spec *= lerp(1.0, specOcclusion(NdotV, gtao, roughness), saturate(gShade.specOcc));
    const float3 diff = albedo * (1.0 / kPi) * (1.0 - F) * (1.0 - metallic) * gtao;
    uint sunCascade = 2u;
    float shadow = 1.0;
    if (gFrame.disableShadow == 0) {
        const float o0 = cascadeOutside(0u, P);
        const float o1 = cascadeOutside(1u, P);
        const float s2 = cascadeShadow(2u, P);
        shadow = s2;
        sunCascade = 2u;
        if (o1 <= 0.0) {
            const float w = saturate(-o1 / 0.18);
            shadow = lerp(s2, cascadeShadow(1u, P), w);
            if (w > 0.5) {
                sunCascade = 1u;
            }
        }
        if (o0 <= 0.0) {
            const float w = saturate(-o0 / 0.18);
            shadow = lerp(shadow, cascadeShadow(0u, P), w);
            if (w > 0.5) {
                sunCascade = 0u;
            }
        }
    }
    if (gShade.sscsAmt > 0.001 && NdotL > 0.05) {
        const float reach = max(gShade.sscsReach, 0.05);
        const float thick = max(gShade.sscsThick, 0.01);
        const float pxCap = clamp(gShade.sscsPx, 4.0, 256.0);
        const int steps = int(clamp(gShade.sscsSteps, 2.0, 32.0));
        const float4 aClip = worldToClip(P + N * 0.02);
        if (aClip.w > 1e-3) {
            const float2 px0 = (aClip.xy / aClip.w * 0.5 + 0.5) * float2(gFrame.extent.xy);
            [loop]
            for (int s = 1; s <= steps; ++s) {
                const float t = reach * (float(s) / float(steps));
                const float3 Q = P + L * t + N * 0.02;
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
                const float along = dot(sceneP - P, L);
                const float bias = min(0.05, thick * 0.3);
                if (gap > bias && gap < thick && along > bias && along < reach) {
                    shadow *= lerp(1.0, 0.0, saturate(gShade.sscsAmt) * saturate(1.0 - along / reach));
                    break;
                }
            }
        }
    }
    shadow *= pomShadow;
    if (gFrame.disableShadow != 0) {
        shadow = 1.0;
    }
    const float3 radiance = gFrame.sunColor.xyz * gFrame.sunDirIntensity.w;
    float3 color = albedo * shAmbient(N, P) * ao + mat.baseColor.rgb * mat.emissive + (diff * ao + spec) * NdotL * shadow * radiance;
    const float4 clipP = worldToClip(P);
    const float2 ndcP = clipP.xy / max(clipP.w, 1e-4) * 0.5 + 0.5;
    const float nearZ = max(gFrame.cameraPos.w, 0.05);
    const float viewZ = max(clipP.w, nearZ);
    const float zFar = max(max(gFrame.sunSplit.z, viewZ), nearZ * 1.01);
    const float zNorm = saturate(log(viewZ / nearZ) / log(zFar / nearZ));
    const uint cx = min(uint(saturate(ndcP.x) * 16.0), 15u);
    const uint cy = min(uint(saturate(ndcP.y) * 9.0), 8u);
    const uint cz = min(uint(zNorm * 24.0), 23u);
    const uint4 cell = gCells[cx + cy * 16u + cz * 144u];
    bool lampContact = false;
    if ((gPass.passMask & 1u) != 0u) {
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
        if (kind != 0) {
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
        }
        float pshadow = 1.0;
        if (gFrame.disableShadow == 0 && shadowSlot >= 0 && shadowSlot < 3) {
            const float3 toPoint = -Lp;
            const float3 ad = abs(toPoint);
            const uint face = ad.x >= ad.y && ad.x >= ad.z ? (toPoint.x > 0.0 ? 0u : 1u)
                : ad.y >= ad.z ? (toPoint.y > 0.0 ? 2u : 3u)
                : (toPoint.z > 0.0 ? 4u : 5u);
            const float4 pclip = mul(gFrame.pointFace[shadowSlot][face], float4(P, 1.0));
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
        color += (diffL * ao + specL) * ndl * atten * lampRad * pshadow;
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
    }
    if (gShade.fogDensity > 0.0008 && gFrame.debugMode == 0) {
        const float3 cam = gFrame.cameraPos.xyz;
        const float3 ray = P - cam;
        const float rayLen = max(length(ray), 0.05);
        const float3 rayDir = ray / rayLen;
        const int fogSteps = 10;
        const float stepLen = min(rayLen, 80.0) / float(fogSteps);
        float transmit = 1.0;
        float3 fogLight = 0.0;
        [loop]
        for (int i = 1; i <= fogSteps; ++i) {
            const float3 Q = cam + rayDir * stepLen * float(i);
            const float height = exp(-max(Q.y - gShade.fogHeight, 0.0) * 0.12);
            const float optical = gShade.fogDensity * height * stepLen;
            const float absorb = 1.0 - exp(-optical);
            const float sunVis = gFrame.disableShadow == 0 ? cascadeLit(2u, Q) : 1.0;
            const float3 sun = gFrame.sunColor.xyz * gFrame.sunDirIntensity.w * sunVis * gShade.fogScatter;
            fogLight += transmit * absorb * (sun + float3(0.04, 0.045, 0.05));
            transmit *= 1.0 - absorb;
        }
        color = color * transmit + fogLight;
    }
    if (gFrame.debugMode == 1) {
        const uint h = tri * 1664525u + 1013904223u;
        gHdr[px] = float4(float(h & 255u) / 255.0, float((h >> 8u) & 255u) / 255.0, float((h >> 16u) & 255u) / 255.0, 1.0);
        return;
    }
    if (gFrame.debugMode == 2) {
        gHdr[px] = float4(N * 0.5 + 0.5, 1.0);
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
    gHdr[px] = float4(color, roughness);
}

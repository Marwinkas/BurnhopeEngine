// Поверхность из visbuffer. Солнце зовёт shadeHit и кладёт её в две картинки. Лампы их читают.

#include "shade_tile.slang"
#include "material_map.slang"

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
    const float span = max(dot(ddx, ddx), dot(ddy, ddy));
    // A derivative longer than ~0.05 UV (world triplanar is often 0.25) asks anisotropy
    // for a mip past the chain. With maxLod unclamped that sample comes back 0.
    if (!(span > 1e-12) || span > 0.0025) {
        float lod = 0.0;
        if (span > 0.0025) {
            lod = clamp(0.5 * log2(span * 1024.0 * 1024.0), 0.0, 7.0);
        }
        return gTex[NonUniformResourceIndex(id)].SampleLevel(gWrap, uv, lod);
    }
    return gTex[NonUniformResourceIndex(id)].SampleGrad(gWrap, uv, ddx, ddy);
}

float4 sampleMap(uint id, MatMap map) {
    if (id >= 768u) {
        return float4(1.0, 1.0, 1.0, 1.0);
    }
    if (map.world == 0u) {
        return sampleGrad(id, map.uv, map.ddx, map.ddy);
    }
    float4 acc = 0.0;
    if (map.w.x >= 0.01) {
        acc += map.w.x * sampleGrad(id, map.uvX, map.dxX, map.dyX);
    }
    if (map.w.y >= 0.01) {
        acc += map.w.y * sampleGrad(id, map.uvY, map.dxY, map.dyY);
    }
    if (map.w.z >= 0.01) {
        acc += map.w.z * sampleGrad(id, map.uvZ, map.dxZ, map.dyZ);
    }
    return acc;
}

float3 decodeTs(float4 texel, MatMap map) {
    float3 tN = texel.xyz * 2.0 - 1.0;
    if (map.flipY != 0u) {
        tN.y = -tN.y;
    }
    if (dot(tN.xy, tN.xy) > 1e-4 && tN.z < 0.05) {
        tN.z = sqrt(max(1.0 - dot(tN.xy, tN.xy), 0.0));
    }
    tN.xy *= map.normalScale;
    return tN;
}

float3 sampleMapNormal(uint id, MatMap map) {
    if (map.world == 0u) {
        return decodeTs(sampleGrad(id, map.uv, map.ddx, map.ddy), map);
    }
    float3 nX = float3(0.0, 0.0, 1.0);
    float3 nY = float3(0.0, 0.0, 1.0);
    float3 nZ = float3(0.0, 0.0, 1.0);
    if (map.w.x >= 0.01) {
        nX = decodeTs(sampleGrad(id, map.uvX, map.dxX, map.dyX), map);
    }
    if (map.w.y >= 0.01) {
        nY = decodeTs(sampleGrad(id, map.uvY, map.dxY, map.dyY), map);
    }
    if (map.w.z >= 0.01) {
        nZ = decodeTs(sampleGrad(id, map.uvZ, map.dxZ, map.dyZ), map);
    }
    return matTriNormal(nX, nY, nZ, map.w, map.axisSign);
}

float2 parallaxOcclusionUv(float2 uv, float2 uvDx, float2 uvDy, float3 viewTS, float ndotv, uint htex, float amount, uint useAlpha) {
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
        const float4 hTexel = sampleGrad(htex, vTexCurrentOffset, uvDx, uvDy);
        const float fCurrHeight = useAlpha != 0u ? hTexel.a : hTexel.r;
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
bool evalHomogeneousUv(float4 c0, float4 c1, float4 c2, float2 uv0, float2 uv1, float2 uv2, float2 ndc, out float2 outUv) {
    const float2 q0 = c0.xy - ndc * c0.w;
    const float2 q1 = c1.xy - ndc * c1.w;
    const float2 q2 = c2.xy - ndc * c2.w;
    const float w0 = q1.x * q2.y - q1.y * q2.x;
    const float w1 = q2.x * q0.y - q2.y * q0.x;
    const float w2 = q0.x * q1.y - q0.y * q1.x;
    const float sum = w0 + w1 + w2;
    if (abs(sum) < 1e-12) {
        outUv = uv0;
        return false;
    }
    const float invSum = 1.0 / sum;
    outUv = uv0 * (w0 * invSum) + uv1 * (w1 * invSum) + uv2 * (w2 * invSum);
    return true;
}

void uvScreenGrad(float3 w0, float3 w1, float3 w2, float2 uv0, float2 uv1, float2 uv2, uint2 px, out float2 ddx, out float2 ddy) {
    ddx = float2(1.0e-5, 0.0);
    ddy = float2(0.0, 1.0e-5);

    const float4 c0 = worldToClip(w0);
    const float4 c1 = worldToClip(w1);
    const float4 c2 = worldToClip(w2);

    const float2 ndcC = (float2(px) + float2(0.5, 0.5)) * gFrame.invExtent * 2.0 - 1.0;
    const float2 ndcX = (float2(px) + float2(1.5, 0.5)) * gFrame.invExtent * 2.0 - 1.0;
    const float2 ndcY = (float2(px) + float2(0.5, 1.5)) * gFrame.invExtent * 2.0 - 1.0;

    float2 uvC, uvX, uvY;
    if (!evalHomogeneousUv(c0, c1, c2, uv0, uv1, uv2, ndcC, uvC) ||
        !evalHomogeneousUv(c0, c1, c2, uv0, uv1, uv2, ndcX, uvX) ||
        !evalHomogeneousUv(c0, c1, c2, uv0, uv1, uv2, ndcY, uvY)) {
        return;
    }

    ddx = uvX - uvC;
    ddy = uvY - uvC;

    if (any(isnan(ddx)) || any(isnan(ddy)) || any(isinf(ddx)) || any(isinf(ddy))) {
        ddx = float2(1.0e-5, 0.0);
        ddy = float2(0.0, 1.0e-5);
        return;
    }
    const float span = max(length(ddx), length(ddy));
    if (span > 1.0) {
        const float scale = 1.0 / span;
        ddx *= scale;
        ddy *= scale;
    }
}

float rainHash(float2 p) {
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float rainNoise(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0 - 2.0 * f);
    const float a = rainHash(i);
    const float b = rainHash(i + float2(1.0, 0.0));
    const float c = rainHash(i + float2(0.0, 1.0));
    const float d = rainHash(i + float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float rainFbm(float2 p) {
    float n = 0.0;
    float a = 0.52;
    [unroll]
    for (int i = 0; i < 3; ++i) {
        n += a * rainNoise(p);
        p = p * 2.07 + 11.3;
        a *= 0.5;
    }
    return n;
}

float2 rainRipple(float2 xz, float time) {
    float2 g = 0.0;
    g.x += cos(dot(xz, float2(46.0, 7.0)) - time * 2.4) * 0.018;
    g.y += cos(dot(xz, float2(-5.0, 63.0)) - time * 1.9) * 0.014;
    g.x += cos(dot(xz, float2(21.0, -3.0)) - time * 1.3) * 0.01;
    g.y += cos(dot(xz, float2(4.0, 27.0)) - time * 1.6) * 0.01;
    return g;
}

bool rainRoof(float3 P) {
    const uint lo = asuint(gFrame.viewTail0.x);
    const uint hi = asuint(gFrame.viewTail0.y);
    if (lo == 0u && hi == 0u) {
        return false;
    }
    RayDesc ray;
    ray.Origin = P + float3(0.0, 0.25, 0.0);
    ray.Direction = float3(0.0, 1.0, 0.0);
    ray.TMin = 0.08;
    ray.TMax = 22.0;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(
        RaytracingAccelerationStructure(uint64_t(lo) | (uint64_t(hi) << 32)),
        RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        0xff,
        ray);
    query.Proceed();
    return query.CommittedStatus() == COMMITTED_TRIANGLE_HIT;
}

float rainSky(float3 P) {
    float open = rainRoof(P) ? 0.0 : 1.0;
    const float2 dir[4] = {
        float2(0.72, 0.16),
        float2(-0.20, 0.70),
        float2(-0.58, -0.42),
        float2(0.24, -0.66),
    };
    [unroll]
    for (int i = 0; i < 4; ++i) {
        open += rainRoof(P + float3(dir[i].x, 0.0, dir[i].y)) ? 0.0 : 1.0;
    }
    return open * 0.2;
}

float rainStream(float sideC, float along, float time, float freq, float salt, out float signedSide) {
    const float u = sideC * freq;
    const float id = floor(u);
    const float f = frac(u) - 0.5;
    const float h = rainHash(float2(id, salt));
    const float width = lerp(0.04, 0.09, frac(h * 4.7));
    const float line = exp(-(f * f) / (width * width));
    const float gate = smoothstep(0.72, 0.88, h);
    const float bead = 0.55 + 0.45 * sin(along * 6.5 - time * 2.4 + h * 19.0);
    signedSide = f * line;
    return line * gate * bead;
}

float rainDrip(float3 P, float3 N, float time, out float side, out float3 across) {
    side = 0.0;
    across = float3(1.0, 0.0, 0.0);
    const float3 grav = float3(0.0, -1.0, 0.0);
    float3 down = grav - N * dot(N, grav);
    const float d2 = dot(down, down);
    if (d2 < 0.06) {
        return 0.0;
    }
    down *= rsqrt(d2);
    across = cross(N, down);
    const float a2 = dot(across, across);
    if (a2 < 1e-6) {
        return 0.0;
    }
    across *= rsqrt(a2);
    const float sideC = dot(P, across);
    const float along = dot(P, down);
    float s0 = 0.0;
    float s1 = 0.0;
    float s2 = 0.0;
    const float a = rainStream(sideC, along, time, 8.0, 1.7, s0);
    const float b = rainStream(sideC, along, time, 14.0, 4.9, s1);
    const float c = rainStream(sideC, along, time, 23.0, 8.2, s2);
    side = clamp((s0 + s1 + s2) * 2.2, -1.0, 1.0);
    return saturate(a * 0.22 + b * 0.16 + c * 0.12);
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
    const VisTri tri = loadVisTri(packed & 0xFFFFF, packed >> 20, px);
    hit.tri = tri.triId;
    const GpuInstance inst = tri.inst;
    const uint i0 = tri.i0;
    const uint i1 = tri.i1;
    const uint i2 = tri.i2;
    const float3 w0 = tri.w0;
    const float3 w1 = tri.w1;
    const float3 w2 = tri.w2;
    float3 N = tri.smoothN;
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
    float2 uvDdx;
    float2 uvDdy;
    uvScreenGrad(w0, w1, w2, uv0e, uv1e, uv2e, px, uvDdx, uvDdy);
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
            float3 B = (e2n * duv1.x - e1n * duv2.x) * invDet;
            T = T - N * dot(N, T);
            B = B - N * dot(N, B);
            if (dot(T, T) > 1e-8 && dot(B, B) > 1e-8) {
                tangent = normalize(T);
                bitangent = normalize(B);
            }
        }
    }
    float3 albedo = mat.baseColor.rgb;
    if ((mat.flags & 4096u) != 0u) {
        albedo *= saturate(mat.baseColor.a);
    }
    float2 uv = float2(
        gVerts[i0].u * buC + gVerts[i1].u * bvC + gVerts[i2].u * bwC,
        gVerts[i0].v * buC + gVerts[i1].v * bvC + gVerts[i2].v * bwC);
    const float3 geoN = dot(N, V) < 0.0 ? -N : N;
    float3 dWx = 0.0;
    float3 dWy = 0.0;
    if ((mat.flags & 256u) != 0u) {
        const uint2 ext = max(gFrame.extent, uint2(1u, 1u));
        const uint2 pxX = uint2(min(px.x + 1u, ext.x - 1u), px.y);
        const uint2 pxY = uint2(px.x, min(px.y + 1u, ext.y - 1u));
        dWx = reconstructWorld(pxX, gDepth.Load(int3(pxX, 0))) - P;
        dWy = reconstructWorld(pxY, gDepth.Load(int3(pxY, 0))) - P;
        const float lx = length(dWx);
        const float ly = length(dWy);
        if (lx > 0.25) {
            dWx *= 0.25 / max(lx, 1e-4);
        }
        if (ly > 0.25) {
            dWy *= 0.25 / max(ly, 1e-4);
        }
    }
    const MatMap surf = matMapUv(mat, uv, uvDdx, uvDdy, P, dWx, dWy, geoN);
    uv = surf.uv;
    uvDdx = surf.ddx;
    uvDdy = surf.ddy;
    uint htex = mat.pad0;
    uint hAlpha = 0u;
    float pom = asfloat(mat.tail0);
    if (pom < 0.001) {
        pom = gShade.pom;
    }
    if (htex == 0u || htex >= 768u) {
        if (mat.orm > 0u && mat.orm < 768u && asfloat(mat.tail0) > 0.001) {
            htex = mat.orm;
            hAlpha = 1u;
        }
    }
    if (surf.world == 0u && pom > 0.001 && htex > 0u && htex < 768u && dot(tangent, tangent) > 0.5) {
        const float3 viewTS = float3(dot(V, tangent), dot(V, bitangent), dot(V, N));
        uv = parallaxOcclusionUv(uv, uvDdx, uvDdy, viewTS, saturate(dot(V, N)), htex, saturate(pom), hAlpha);
    }
    const uint ntex = mat.normalTex;
    if ((mat.flags & 32u) == 0u && ntex > 0u && ntex < 768u && (surf.world != 0u || dot(tangent, tangent) > 0.5)) {
        const float3 tN = sampleMapNormal(ntex, surf);
        if (surf.world != 0u) {
            N = tN;
            normalLen = saturate(length(tN));
        } else {
            normalLen = saturate(length(tN.xy));
            N = normalize(tangent * tN.x + bitangent * tN.y + N * tN.z);
        }
    }
    const float rainAmt = saturate(gFrame.viewPad.w);
    const float rainTime = gFrame.viewPad.z;
    const float wetScale = saturate(gFrame.viewTail0.z);
    const float ripScale = saturate(gFrame.viewTail0.w);
    const float openSky = (rainAmt > 0.05 || wetScale > 0.02) ? rainSky(P) : 1.0;
    const float wetField = rainFbm(P.xz * 0.045 + float2(P.y, 0.0) * 0.02);
    const float wet = wetScale * lerp(0.62, 1.0, wetField) * openSky;
    const float upFace = saturate((geoN.y - 0.15) / 0.55);
    const float puddle = upFace * openSky * saturate(wetScale - 0.35) * lerp(0.25, 1.0, wetField);
    const float wall = saturate((0.72 - abs(geoN.y)) / 0.28) * rainAmt * openSky;
    float dripSide = 0.0;
    float3 dripAcross = float3(1.0, 0.0, 0.0);
    const float drip = wall > 0.02 ? rainDrip(P, geoN, rainTime, dripSide, dripAcross) : 0.0;
    if (wet > 0.02 && dot(tangent, tangent) > 0.5) {
        const float2 duv = uv * 24.0;
        const float hC = frac(sin(dot(duv, float2(127.1, 311.7))) * 43758.5453);
        const float hR = frac(sin(dot(duv + float2(0.5, 0.0), float2(127.1, 311.7))) * 43758.5453);
        const float hU = frac(sin(dot(duv + float2(0.0, 0.5), float2(127.1, 311.7))) * 43758.5453);
        float3 n1 = float3(0.0, 0.0, 1.0);
        float3 n2 = normalize(float3(hC - hR, hC - hU, 0.55));
        n1 += float3(0.0, 0.0, 1.0);
        n2 *= float3(-1.0, -1.0, 1.0);
        const float3 blended = n1 * dot(n1, n2) / max(n1.z, 1e-3) - n2;
        const float3 detail = normalize(tangent * blended.x + bitangent * blended.y + N * blended.z);
        N = normalize(lerp(N, detail, lerp(0.22, 0.05, wet)));
    }
    N = normalize(lerp(N, geoN, wet * 0.42));
    if (upFace > 0.02 && ripScale > 0.001 && rainAmt > 0.02 && openSky > 0.0) {
        const float2 rip = rainRipple(P.xz, rainTime) * ripScale;
        const float3 water = normalize(float3(rip.x, 1.0, rip.y));
        N = normalize(lerp(N, water, upFace * rainAmt * openSky * 0.12));
    }
    if (drip > 0.08) {
        N = normalize(N + dripAcross * dripSide * drip * 0.08);
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
        const float4 texel = sampleMap(tex, surf);
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
    float roughMap = 1.0;
    float metalMap = 1.0;
    if (mat.orm > 0 && mat.orm < 768) {
        const float3 orm = sampleMap(mat.orm, surf).rgb;
        ao = orm.r > 0.02 ? saturate(orm.r) : 1.0;
        roughMap = saturate(orm.g);
        metalMap = saturate(orm.b);
    }
    if (mat.pad2 > 0u && mat.pad2 < 768u) {
        const float3 roughTex = sampleMap(mat.pad2, surf).rgb;
        roughMap = saturate(roughTex.g > 0.02 ? roughTex.g : roughTex.r);
    }
    if (mat.pad3 > 0u && mat.pad3 < 768u) {
        const float3 metalTex = sampleMap(mat.pad3, surf).rgb;
        metalMap = saturate(max(metalTex.r, metalTex.b));
    }
    float metallic = saturate(mat.metallic * metalMap);
    float roughness = clamp(mat.roughness * roughMap, 0.045, 1.0);
    if (mat.aoTex > 0u && mat.aoTex < 768u) {
        ao *= saturate(sampleMap(mat.aoTex, surf).r);
    }
    ao = lerp(1.0, ao, saturate(mat.aoStrength));
    const float normalGap = saturate(1.0 - normalLen);
    const float kernelRough = min(2.0 * normalGap * normalGap * max(gShade.toksvig, 0.0), 0.18);
    roughness = sqrt(saturate(roughness * roughness + kernelRough));
    float coat = mat.clearcoat;
    if (coat <= 0.0 && (mat.flags & 64u) != 0u) {
        coat = 1.0;
    }
    coat = saturate(coat);
    float coatPerceptual = mat.clearcoatRough;
    if (coatPerceptual < 0.04) {
        coatPerceptual = 0.045;
    }
    if (coat > 0.0) {
        roughness = lerp(roughness, max(roughness, coatPerceptual), coat);
    }
    albedo = lerp(albedo, albedo * albedo, saturate(wet * 0.8 + puddle * 0.2));
    albedo = lerp(albedo, albedo * 0.97, saturate(drip));
    roughness = lerp(roughness, 0.2, wet * 0.85);
    roughness = lerp(roughness, 0.03, puddle * lerp(0.35, 1.0, wetField));
    roughness = lerp(roughness, roughness * 0.92, saturate(drip) * wall);
    roughness = clamp(roughness, 0.02, 1.0);
    const float a = roughness * roughness;
    float reflectance = mat.reflectance;
    if (reflectance < 0.01 || reflectance > 1.0) {
        reflectance = asfloat(mat.pad1);
        if (reflectance < 0.01 || reflectance > 1.0) {
            reflectance = 0.5;
        }
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
    float gain = mat.emissive;
    if (mat.emissiveGain > 0.0) {
        gain = mat.emissiveGain;
    }
    float3 emitCol = float3(mat.emitR, mat.emitG, mat.emitB);
    if (dot(emitCol, emitCol) < 1e-8) {
        emitCol = float3(1.0, 1.0, 1.0);
    }
    hit.emissive = emitCol * gain * 8.0;
    if (mat.emissiveTex > 0u && mat.emissiveTex < 768u) {
        hit.emissive *= sampleMap(mat.emissiveTex, surf).rgb;
    }
    if ((mat.flags & 1024u) != 0u) {
        hit.emissive = albedo;
        hit.diff = 0.0;
        hit.spec = 0.0;
        hit.coat = 0.0;
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
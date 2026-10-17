#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "rc_common.slang"
#include "rc_geom.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] Texture2D<float4> gHdrIn;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(24, 0)]] SamplerState gWrap;

float3 rcSunDir() {
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    return dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
}

float3 rcSunColor() {
    float3 sunCol = gSun.sunColor.rgb;
    if (max(sunCol.r, max(sunCol.g, sunCol.b)) < 0.02) {
        sunCol = float3(1.0, 0.97, 0.92);
    }
    return sunCol;
}

float rcReach(uint tag) {
    const uint lod = rcTagLod(tag);
    return max(gPush.baseLength, 0.5) * exp2(float(lod));
}

float rcSunIntensity() {
    float inten = gSun.sunDirIntensity.w;
    if (inten < 0.05) {
        inten = 8.0;
    }
    return inten;
}

uint rcHitCascade(float t, uint tag) {
    float limit = rcT0(tag);
    [unroll]
    for (uint n = 0u; n < 3u; ++n) {
        if (t <= limit) {
            return n;
        }
        limit *= 4.0;
    }
    return 3u;
}

RaytracingAccelerationStructure rcScene() {
    return RaytracingAccelerationStructure(uint64_t(rcLoad(112u)) | (uint64_t(rcLoad(116u)) << 32));
}

float rcSunVis(float3 P, float3 N, float ndl) {
    if (ndl <= 0.005) {
        return 0.0;
    }
    if (gFrame.disableShadow != 0u) {
        return 1.0;
    }
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> q;
    RayDesc ray;
    ray.Origin = P + N * 0.03;
    ray.Direction = rcSunDir();
    ray.TMin = 0.02;
    ray.TMax = 500.0;
    q.TraceRayInline(rcScene(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xff, ray);
    q.Proceed();
    return q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0;
}

void rcTally(bool tally, bool miss, float3 Le, float ndl, float vis, float albedoPeak, float metal, float dist) {
    if (!tally || gPush.frameIndex != 0u) {
        return;
    }
    uint ignored = 0u;
    if (miss) {
        gRc.InterlockedAdd(180u, 1u, ignored);
        return;
    }
    gRc.InterlockedAdd(176u, 1u, ignored);
    const float lum = min(max(Le.r, max(Le.g, Le.b)), 64.0);
    if (lum < 1e-4) {
        gRc.InterlockedAdd(184u, 1u, ignored);
    }
    gRc.InterlockedAdd(188u, uint(lum * 256.0 + 0.5), ignored);
    gRc.InterlockedAdd(192u, uint(saturate(vis) * 256.0 + 0.5), ignored);
    gRc.InterlockedAdd(196u, uint(saturate(ndl) * 256.0 + 0.5), ignored);
    gRc.InterlockedAdd(232u, uint(min(dist, 512.0) * 256.0 + 0.5), ignored);
    uint prev = 0u;
    gRc.InterlockedCompareExchange(216u, 0u, 1u, prev);
    if (prev == 0u) {
        rcStore(200u, asuint(albedoPeak));
        rcStore(204u, asuint(vis));
        rcStore(208u, asuint(ndl));
        rcStore(212u, asuint(metal));
    }
}

float3 rcLeWorld(float3 hitP, float3 n, float3 dir, float3 albedo, float metal, float rough, float emissive) {
    const float3 L = rcSunDir();
    const float3 V = -dir;
    const float ndl = saturate(dot(n, L));
    const float ndv = saturate(dot(n, V));
    const float diff = hammonDiffuse(ndl, max(ndv, 0.15), dot(V, L), rough);
    // Усиливаем чистоту цвета при отскоке, чтобы цветные объекты (жёлтый, красный, синий кубы и стены) давали яркий рефлекс!
    const float luma = dot(albedo, float3(0.299, 0.587, 0.114));
    const float3 satAlbedo = max(lerp(float3(luma, luma, luma), albedo, 1.35), 0.0);
    const float3 tint = satAlbedo * lerp(1.0, 0.75, metal);
    const float3 color = tint * max(diff, 0.0);
    const float vis = rcSunVis(hitP, n, ndl);
    const float bounceScale = max(gRcParams.bounce, 0.1) * 3.2;
    const float3 bounced = color * vis * rcSunColor() * rcSunIntensity() * max(gRcParams.sunScale, 0.0) * bounceScale;
    const float3 glow = max(albedo, 0.0) * max(emissive, 0.0) * 4.0;
    return max(bounced + glow, 0.0);
}

bool rcHitColor(uint prim, float2 bary, float3 dir, out float3 albedo, out float metal, out float rough, out float emissive, out float3 n) {
    const RcHitVert a = rcVert(prim * 3u);
    const RcHitVert b = rcVert(prim * 3u + 1u);
    const RcHitVert c = rcVert(prim * 3u + 2u);
    const float w0 = saturate(1.0 - bary.x - bary.y);
    const float w1 = saturate(bary.x);
    const float w2 = saturate(bary.y);
    const float2 uv = float2(a.u, a.v) * w0 + float2(b.u, b.v) * w1 + float2(c.u, c.v) * w2;
    const float3 face = cross(b.p - a.p, c.p - a.p);
    float3 geomN = dot(face, face) > 1e-12 ? normalize(face) : float3(0.0, 1.0, 0.0);
    const float3 vN = a.n * w0 + b.n * w1 + c.n * w2;
    if (dot(vN, vN) > 1e-4) {
        if (dot(geomN, vN) < 0.0) {
            geomN = -geomN;
        }
        n = normalize(vN);
    } else {
        n = geomN;
    }

    uint matCount;
    uint matStride;
    gMat.GetDimensions(matCount, matStride);
    MaterialGpu mat;
    mat.baseColor = float4(0.5, 0.5, 0.5, 1.0);
    mat.roughness = 1.0;
    mat.metallic = 0.0;
    mat.emissive = 0.0;
    mat.flags = 0u;
    mat.albedo = 0u;
    mat.normalTex = 0u;
    mat.orm = 0u;
    mat.emissiveTex = 0u;
    mat.pad0 = 0u;
    mat.pad1 = 0u;
    mat.pad2 = 0u;
    mat.pad3 = 0u;
    if (matCount > 0u) {
        mat = gMat[min(a.mat, matCount - 1u)];
    }
    const bool foliage = (mat.flags & 32u) != 0u;
    metal = saturate(mat.metallic);
    rough = saturate(mat.roughness);
    emissive = max(mat.emissive, 0.0);
    albedo = max(mat.baseColor.rgb, 0.0);
    if (mat.albedo < 768u) {
        const float4 texel = gTex[NonUniformResourceIndex(mat.albedo)].SampleLevel(gWrap, uv, 0.0);
        if (texel.a < 0.35 && (foliage || mat.albedo > 0u)) {
            return false;
        }
        const float peak = max(texel.r, max(texel.g, texel.b));
        if (peak > 0.0) {
            albedo *= max(texel.rgb, 0.0);
        }
    }
    if (foliage) {
        if (dot(n, dir) > 0.0) {
            n = -n;
        }
    } else if (dot(geomN, dir) > 0.05) {
        albedo = 0.0;
        emissive = 0.0;
    }
    return true;
}

void rcCast(float3 origin, float3 dir, float reach, float backSkip, bool tally, out float hitT, out float3 Le, out bool miss) {
    hitT = reach;
    Le = 0.0;
    miss = true;
    float traveled = 0.0;
    [loop]
    for (uint hop = 0u; hop < 6u; ++hop) {
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        RayDesc ray;
        ray.Origin = origin + dir * traveled;
        ray.Direction = dir;
        ray.TMin = 0.005;
        ray.TMax = max(reach - traveled, 0.006);
        q.TraceRayInline(rcScene(), RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
        q.Proceed();
        if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
            rcTally(tally, true, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
            return;
        }
        const float hwT = q.CommittedRayT();
        const bool front = q.CommittedTriangleFrontFace();
        if (!front && (traveled + hwT) < backSkip) {
            traveled += hwT + 0.02;
            if (traveled >= reach) {
                rcTally(tally, true, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
                return;
            }
            continue;
        }
        const uint prim = q.CommittedPrimitiveIndex();
        const float2 bary = q.CommittedTriangleBarycentrics();
        float3 albedo;
        float metal;
        float rough;
        float emissive;
        float3 n;
        if (!rcHitColor(prim, bary, dir, albedo, metal, rough, emissive, n)) {
            traveled += hwT + 0.02;
            if (traveled >= reach) {
                rcTally(tally, true, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
                return;
            }
            continue;
        }
        const float3 hitP = origin + dir * (traveled + hwT);
        miss = false;
        hitT = traveled + hwT;
        Le = rcLeWorld(hitP, n, dir, albedo, metal, rough, emissive);
        const float ndl = saturate(dot(n, rcSunDir()));
        const float vis = tally ? rcSunVis(hitP, n, ndl) : 0.0;
        rcTally(tally, false, Le, ndl, vis, max(albedo.r, max(albedo.g, albedo.b)), metal, hitT);
        return;
    }
    rcTally(tally, true, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
}

// Все 32 луча ориентированы в полусферу нормали N (Алгоритм 3 статьи):
// ни один луч не тратится впустую в землю под собой, поэтому цвет передаётся максимально ярко!
float3 rcSampleDir(uint dirId, uint subRay, uint cellSeed, float3 N) {
    const float2 jitter = subRay == 0u ? float2(0.5, 0.5) : rcR2(cellSeed * 256u + dirId * 8u + subRay);
    const float u = (float(dirId & 7u) + jitter.x) / 8.0;
    const float v = (float(dirId >> 3u) + jitter.y) / 4.0;
    float3 w = rcDecodeDir(float2(u, v));
    if (dot(w, N) < 0.0) {
        w = -w;
    }
    return w;
}

void rcSplitDeposit(uint probe0, uint tag, float3 dir, float hitT, float3 Le, bool miss) {
    if (probe0 == kInvalid) {
        return;
    }
    if (miss) {
        const float skyUp = saturate(dir.y);
        const float sunCos = saturate(dot(dir, rcSunDir()));
        const float sunLobe = pow(sunCos, 8.0) * step(0.0, dir.y);
        const float3 dome = float3(0.018, 0.028, 0.048) * (0.3 + 0.7 * skyUp);
        const float3 solar = rcSunColor() * (0.09 * sunLobe);
        const float3 skyCol = (dome + solar) * rcSunIntensity() * max(gRcParams.sunScale, 0.0);
        uint probe = probe0;
        [unroll]
        for (uint k = 0u; k < 4u; ++k) {
            if (probe == kInvalid) {
                return;
            }
            if (k < 3u) {
                rcDeposit(k, probe, rcDirIndex(dir, k), float3(0.0, 0.0, 0.0), 1.0);
            } else {
                rcDeposit(3u, probe, rcDirIndex(dir, 3u), skyCol, 0.0);
            }
            rcStore(rcMetaAddr(k, probe) + 24u, 1u);
            probe = rcParent(k, probe);
        }
        return;
    }

    const uint n = rcHitCascade(hitT, tag);
    uint probe = probe0;
    [unroll]
    for (uint k = 0u; k < 4u; ++k) {
        if (probe == kInvalid) {
            return;
        }
        if (k < n) {
            rcDeposit(k, probe, rcDirIndex(dir, k), float3(0.0, 0.0, 0.0), 1.0);
        } else if (k == n) {
            rcDeposit(k, probe, rcDirIndex(dir, k), max(Le, 0.0), 0.0);
        } else {
            return;
        }
        rcStore(rcMetaAddr(k, probe) + 24u, 1u);
        if (k == n) {
            return;
        }
        probe = rcParent(k, probe);
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void rcTrace(uint3 dtid : SV_DispatchThreadID) {
    const uint dirId = dtid.x & 31u;
    const uint probe = dtid.x >> 5u;
    if (probe >= min(min(rcCount(0u), rcProbeCap(0u)), 65535u)) {
        return;
    }
    const uint meta = rcMetaAddr(0u, probe);
    if (rcLoad(meta + 28u) == 0u) {
        return;
    }
    const int3 cell = int3(asint(rcLoad(meta)), asint(rcLoad(meta + 4u)), asint(rcLoad(meta + 8u)));
    const uint tag = rcLoad(meta + 12u);
    const float3 P = float3(asfloat(rcLoad(meta + 32u)), asfloat(rcLoad(meta + 36u)), asfloat(rcLoad(meta + 40u)));
    float3 N = float3(asfloat(rcLoad(meta + 44u)), asfloat(rcLoad(meta + 48u)), asfloat(rcLoad(meta + 52u)));
    if (dot(N, N) < 0.5) {
        N = float3(0.0, 1.0, 0.0);
    } else {
        N = normalize(N);
    }
    const float sp = max(rcSpacing(0u, tag), 0.1);
    const float3 rayOrigin = P + N * max(0.03, sp * 0.08);
    const float backSkip = max(0.20, sp * 0.5);
    const uint64_t key = rcPackKey(cell, tag);
    const uint cellSeed = rcSlot(uint(key), uint(key >> 32), 0xFFFFu);
    const uint rayMult = clamp(gPush.cascadeIndex, 1u, 8u);
    const float reach = rcReach(tag);
    [loop]
    for (uint r = 0u; r < rayMult; ++r) {
        const float3 dir = rcSampleDir(dirId, r, cellSeed, N);
        float hitT;
        float3 Le;
        bool miss;
        rcCast(rayOrigin, dir, reach, backSkip, r == 0u && dirId == 0u && (probe & 31u) == 0u, hitT, Le, miss);
        rcSplitDeposit(probe, tag, dir, hitT, Le, miss);
    }
}
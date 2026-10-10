#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "radiance_rc.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;

// Луч из видимой точки режется по дистанции t_n = t0 * 4^n.
// Каскады ниже попадания получают прозрачность, каскад попадания — сияние, выше — ничего.

float4 loadPos(uint vertex) {
    const uint byteOff = rcLoad(12) + vertex * 16u;
    return float4(asfloat(rcLoad(byteOff)), asfloat(rcLoad(byteOff + 4u)), asfloat(rcLoad(byteOff + 8u)), asfloat(rcLoad(byteOff + 12u)));
}

float3 rayDir(uint probe, float3 N) {
    const uint meta = metaBase(0u) + probe * 32u;
    uint seq = 0u;
    gRc.InterlockedAdd(meta + 16u, 1u, seq);
    const float2 jitter = float2(asfloat(rcLoad(16)), asfloat(rcLoad(20)));
    const float2 uv = frac(float2(float(seq) * 0.754877666, float(seq) * 0.569840296) + jitter);
    float3 w = decodeDir(uv);
    if (dot(w, N) < 0.0) {
        w = -w;
    }
    return w;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcTrace(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0u) {
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 N = reconstructNormalFromDepth(gDepth, px, P);
    const float cheb = max(abs(P.x - gFrame.cameraPos.x), max(abs(P.y - gFrame.cameraPos.y), abs(P.z - gFrame.cameraPos.z)));
    const uint lod = min(uint(log2(max(cheb / max(gRcParams.spacing, 0.05), 1.0))), 5u);
    const int3 cell0 = nearestCell(P, 0u, lod);
    const uint probe0 = insertProbe(0u, packKey(cell0, lod), cell0, lod);
    if (probe0 == 0xFFFFFFFFu) {
        return;
    }
    RaytracingAccelerationStructure tlas = RaytracingAccelerationStructure((uint64_t(rcLoad(4)) << 32) | uint64_t(rcLoad(0)));
    const uint rays = uint(clamp(int(gRcParams.rays), 1, 32));
    const float reach = max(gRcParams.maxDist, 1.0);
    const float3 sun = normalize(gSun.sunDirIntensity.xyz);
    const float3 sunCol = gSun.sunColor.rgb * gSun.sunDirIntensity.w;
    float t0 = 1.6 * spacingAt(0u, lod);
    if (lod > 0u) {
        t0 *= 0.9;
    }
    [loop]
    for (uint r = 0u; r < rays; ++r) {
        const float3 dir = rayDir(probe0, N);
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        RayDesc ray;
        ray.Origin = P + N * 0.02;
        ray.Direction = dir;
        ray.TMin = 0.0;
        ray.TMax = reach;
        q.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
        q.Proceed();
        float3 Le = 0.0;
        float hitT = reach;
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            hitT = q.CommittedRayT();
            const uint prim = q.CommittedPrimitiveIndex();
            const float3 a = loadPos(prim * 3u).xyz;
            const float3 b = loadPos(prim * 3u + 1u).xyz;
            const float3 c = loadPos(prim * 3u + 2u).xyz;
            float3 n = cross(b - a, c - a);
            n = dot(n, n) > 1e-12 ? normalize(n) : dir;
            if (dot(n, dir) > 0.0) {
                n = -n;
            }
            const uint matId = asuint(loadPos(prim * 3u).w);
            const float3 albedo = gMat[matId].baseColor.rgb;
            const float3 hitP = P + dir * hitT + n * 0.02;
            RayQuery<RAY_FLAG_FORCE_OPAQUE> shadow;
            RayDesc sray;
            sray.Origin = hitP;
            sray.Direction = sun;
            sray.TMin = 0.0;
            sray.TMax = reach;
            shadow.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, sray);
            shadow.Proceed();
            const float ndl = shadow.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 0.0 : saturate(dot(n, sun));
            Le = albedo * sunCol * ndl + albedo * gMat[matId].emissive;
        } else {
            Le = float3(0.42, 0.52, 0.72) * saturate(dir.y * 0.6 + 0.35);
            hitT = reach;
        }
        uint hitC = 3u;
        [unroll]
        for (uint n = 0u; n < 4u; ++n) {
            const float tend = t0 * exp2(float(n) * 2.0);
            if (hitT <= tend) {
                hitC = n;
                break;
            }
        }
        [unroll]
        for (uint k = 0u; k < 4u; ++k) {
            if (k > hitC) {
                break;
            }
            const int3 cell = k == 0u ? cell0 : nearestCell(P, k, lod);
            const uint probe = k == 0u ? probe0 : insertProbe(k, packKey(cell, lod), cell, lod);
            const float3 J = k == hitC ? Le : float3(0.0, 0.0, 0.0);
            const float beta = k == hitC ? 0.0 : 1.0;
            depositDir(k, probe, dirIndex(dir, k), J, beta);
        }
    }
}

float3 irrTexel(uint probe, uint x, uint y) {
    const uint off = irrBase() + probe * 576u + (y * 6u + x) * 16u;
    return float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
}

float3 sampleIrr(uint probe, float3 N) {
    const float2 p = clamp(octEncode(N) * 6.0 - 0.5, 0.0, 5.0);
    const uint2 i = uint2(p);
    const float2 f = frac(p);
    const uint x1 = min(i.x + 1u, 5u);
    const uint y1 = min(i.y + 1u, 5u);
    const float3 a = irrTexel(probe, i.x, i.y);
    const float3 b = irrTexel(probe, x1, i.y);
    const float3 c = irrTexel(probe, i.x, y1);
    const float3 d = irrTexel(probe, x1, y1);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float3 irradianceLod(float3 P, float3 N, uint lod) {
    const float sp = spacingAt(0u, lod);
    const float3 g = P / sp - 0.5;
    const int3 i0 = int3(floor(g));
    const float3 f = frac(g);
    float3 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const int3 cell = i0 + int3(corner & 1u, (corner >> 1) & 1u, (corner >> 2) & 1u);
        const float wx = (corner & 1u) != 0u ? f.x : 1.0 - f.x;
        const float wy = ((corner >> 1) & 1u) != 0u ? f.y : 1.0 - f.y;
        const float wz = ((corner >> 2) & 1u) != 0u ? f.z : 1.0 - f.z;
        const float w = wx * wy * wz;
        const uint probe = lookupProbe(0u, packKey(cell, lod));
        if (probe == 0xFFFFFFFFu || w <= 0.0) {
            continue;
        }
        acc += sampleIrr(probe, N) * w;
        wsum += w;
    }
    return wsum > 1e-4 ? acc / wsum : 0.0;
}

float3 irradianceAt(float3 world, float3 N) {
    const float cheb = max(abs(world.x - gFrame.cameraPos.x), max(abs(world.y - gFrame.cameraPos.y), abs(world.z - gFrame.cameraPos.z)));
    const float lodf = log2(max(cheb / max(gRcParams.spacing, 0.05), 1.0));
    const uint lod0 = min(uint(lodf), 5u);
    const uint lod1 = min(lod0 + 1u, 5u);
    const float3 e0 = irradianceLod(world, N, lod0);
    const float3 e1 = irradianceLod(world, N, lod1);
    return lerp(e0, e1, frac(lodf));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcApply(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    if ((gPass.passMask & 256u) == 0u) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gFrame.debugMode == 1 || gFrame.debugMode == 2) {
        return;
    }
    const uint vis = gVis.Load(int3(px, 0));
    if (vis == 0u) {
        return;
    }
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 N = reconstructNormalFromDepth(gDepth, px, P);
    const uint packed = vis - 1u;
    const MaterialGpu mat = gMat[gInstances[packed & 0xFFFFFu].materialId];
    const float3 gi = min(irradianceAt(P, N), gRcParams.cap) * max(gRcParams.intensity, 0.0) * 0.35;
    if (max(gi.r, max(gi.g, gi.b)) < 0.002) {
        return;
    }
    if (gFrame.debugMode == 4) {
        gHdr[px] = float4(gi, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += mat.baseColor.rgb * gi;
    gHdr[px] = hdr;
}

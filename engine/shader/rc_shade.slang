#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"
#include "rc_common.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(32, 0)]] Texture2D<float> gAo;
[[vk::binding(47, 0)]] Texture2D<float> gContact;

#include "rc_surface.slang"

RaytracingAccelerationStructure rcScene() {
    return RaytracingAccelerationStructure(uint64_t(rcLoad(112u)) | (uint64_t(rcLoad(116u)) << 32));
}

// Чистая дистанционная контактная тень вокруг нормали N (без дублирования солнечной тени!):
// чем ближе точка P к любому объекту (кубу, кусту, столбу фонаря, карнизу), тем темнее стык!
float rcRayDistFactor(float3 origin, float3 dir, float maxRange) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.01;
    ray.TMax = maxRange;
    q.TraceRayInline(rcScene(), RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return 1.0;
    }
    const float hitT = q.CommittedRayT();
    return lerp(0.15, 1.0, smoothstep(0.0, maxRange, hitT));
}

float3 rcSunDir() {
    const float3 sunRaw = gSun.sunDirIntensity.xyz;
    return dot(sunRaw, sunRaw) > 1e-8 ? normalize(sunRaw) : float3(0.0, 1.0, 0.0);
}

float rcRayHitDist(float3 origin, float3 dir, float maxRange) {
    RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = dir;
    ray.TMin = 0.012;
    ray.TMax = maxRange;
    q.TraceRayInline(rcScene(), RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
    q.Proceed();
    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT) {
        return maxRange;
    }
    return q.CommittedRayT();
}

float rcContactOcclusion(float3 P, float3 N) {
    if (gFrame.disableShadow != 0u) {
        return 1.0;
    }
    const float3 L = rcSunDir();
    if (dot(N, L) <= 0.01) {
        return 1.0;
    }
    const float3 origin = P + N * 0.02;
    const float sunMax = 16.0;
    const float sunDist = rcRayHitDist(origin, L, sunMax);
    if (sunDist >= sunMax) {
        return 1.0;
    }
    // Тень ТОЛЬКО со стороны от солнца: вплотную к объекту (sunDist -> 0) темнее (0.15), дальше мягче (0.72)
    return lerp(0.15, 0.72, smoothstep(0.0, 4.5, sunDist));
}
float3 rcIrrTexel(uint probe, uint x, uint y) {
    const uint off = sliceBase() + rcIrrRel() + probe * rcIrrStride() + (y * 6u + x) * 16u;
    return float3(asfloat(rcLoad(off)), asfloat(rcLoad(off + 4u)), asfloat(rcLoad(off + 8u)));
}

float3 rcSampleIrr(uint probe, float3 N) {
    const float2 octTexel = octEncode(N) * 4.0 + 1.0;
    const float2 p = clamp(octTexel - 0.5, 0.0, 4.999);
    const uint2 i = uint2(p);
    const float2 f = frac(p);
    const uint2 i1 = min(i + 1u, uint2(5u, 5u));
    const float3 a = rcIrrTexel(probe, i.x, i.y);
    const float3 b = rcIrrTexel(probe, i1.x, i.y);
    const float3 c = rcIrrTexel(probe, i.x, i1.y);
    const float3 d = rcIrrTexel(probe, i1.x, i1.y);
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

bool rcCellEnergy(int3 cell, uint tag, float3 P, float3 N, float sp, out float3 e, out float visW) {
    e = 0.0;
    visW = 0.0;
    const uint probe = rcLookup(0u, rcPackKey(cell, tag));
    if (probe == kInvalid) {
        return false;
    }
    const uint meta = rcMetaAddr(0u, probe);
    if (rcLoad(meta + 24u) == 0u) {
        return false;
    }
    const float3 probeP = float3(asfloat(rcLoad(meta + 32u)), asfloat(rcLoad(meta + 36u)), asfloat(rcLoad(meta + 40u)));
    const float3 probeN = float3(asfloat(rcLoad(meta + 44u)), asfloat(rcLoad(meta + 48u)), asfloat(rcLoad(meta + 52u)));
    const float planeDist = dot(probeP - P, N) / max(sp, 1e-3);
    const float backW = smoothstep(-0.6, 0.05, planeDist);
    const float normW = saturate(dot(probeN, N) * 0.5 + 0.6);
    visW = max(backW * normW, 0.04);
    e = max(rcSampleIrr(probe, N), 0.0);
    return e.x == e.x;
}

float3 rcIrradianceLod(float3 P, float3 N, uint lod, out float weight) {
    const uint tag = lod & 7u;
    const float sp = max(rcSpacing(0u, tag), 1e-3);
    // Фиксированный малый сдвиг 2 см вместо sp * 0.2 — сохраняет чёткость на рельефе и брусчатке!
    const float3 sampleP = P + N * 0.02;
    const float3 g = sampleP / sp - 0.5;
    const int3 i0 = int3(floor(g));
    const float3 rawF = saturate(frac(g));
    const float3 f = rawF * rawF * (3.0 - 2.0 * rawF);
    float3 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (uint corner = 0u; corner < 8u; ++corner) {
        const int3 cell = i0 + int3(corner & 1u, (corner >> 1u) & 1u, (corner >> 2u) & 1u);
        const float wx = (corner & 1u) != 0u ? f.x : 1.0 - f.x;
        const float wy = ((corner >> 1u) & 1u) != 0u ? f.y : 1.0 - f.y;
        const float wz = ((corner >> 2u) & 1u) != 0u ? f.z : 1.0 - f.z;
        const float triW = wx * wy * wz;
        if (triW <= 1e-5) {
            continue;
        }
        float3 e = 0.0;
        float visW = 0.0;
        if (!rcCellEnergy(cell, tag, P, N, sp, e, visW)) {
            continue;
        }
        const float w = triW * visW;
        acc += e * w;
        wsum += w;
    }
    weight = wsum;
    return wsum > 1e-5 ? acc / wsum : 0.0;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcShade(uint3 dtid : SV_DispatchThreadID) {
    if ((gPass.passMask & 262144u) == 0u) {
        return;
    }
    if (gFrame.debugMode != 0u && gFrame.debugMode != 4u) {
        return;
    }
    float depth = 0.0;
    if (!rcPixel(dtid.xy, depth)) {
        return;
    }
    const uint2 px = dtid.xy;
    const float3 P = reconstructWorld(px, depth);
    float3 N = octDecode(gSurf1[px].xy);
    if (dot(N, N) < 0.5) {
        N = reconstructNormalFromDepth(gDepth, px, P);
    } else {
        N = normalize(N);
    }
    const float cheb = rcChebDist(P, gFrame.cameraPos.xyz);
    const float d0 = rcLodBaseRadius();
    const uint lod0 = cheb <= d0 ? 0u : min(uint(floor(log2(cheb / d0))), 5u);

    float w0 = 0.0;
    float3 incoming = rcIrradianceLod(P, N, lod0, w0);

    const float lodEnd = d0 * exp2(float(lod0));
    if (cheb >= lodEnd * 0.85 && lod0 < 5u) {
        float w1 = 0.0;
        const float3 inc1 = rcIrradianceLod(P, N, lod0 + 1u, w1);
        if (w1 > 1e-5) {
            const float t = saturate((cheb - lodEnd * 0.85) / (lodEnd * 0.15));
            incoming = w0 > 1e-5 ? lerp(incoming, inc1, t) : inc1;
            w0 = max(w0, w1);
        }
    }

    const float gtao = clamp(gSurf1[px].w, 0.25, 1.0);
    const float contactAo = rcContactOcclusion(P, N);
    const float aoMod = gtao * contactAo;

    const float3 albedo = max(gSurf0[px].rgb, 0.0);
    const float metal = saturate(gSurf1[px].z);
    const float3 tint = albedo * lerp(1.0, 0.35, metal);
    const float rcInt = gRcParams.intensity > 1e-4 ? gRcParams.intensity : 1.0;
    const float shInt = gShade.pad1 > 1e-4 ? gShade.pad1 : 1.0;
    const float gain = rcInt * shInt;
    const float3 gi = incoming * tint * gain * aoMod;
    if (gFrame.debugMode == 4u) {
        gHdr[px] = float4(max(gi, 0.0), 1.0);
        return;
    }
    if (w0 <= 1e-5) {
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += max(gi, 0.0);
    gHdr[px] = hdr;
}
// Один луч. Половина кадра на балансе, полный кадр и конус GGX на ультра.
// Адрес структуры 0 — промах в кубмап, луч не пускается.

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"
#include "radiance_rc.slang"

[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(16, 0)]] Texture2D<float4> gCube;
[[vk::binding(24, 0)]] SamplerState gWrap;
[[vk::binding(36, 0)]] RWTexture2D<float4> gReflect;

float3 sampleCube(float3 dir) {
    uint face = 0u;
    float2 uv = 0.0;
    cubeDirToFaceUV(dir, face, uv);
    return gCube.SampleLevel(gWrap, float2((uv.x + float(face)) / 6.0, uv.y), 0.0).rgb;
}

float4 loadPos(uint vertex) {
    const uint byteOff = rcLoad(12) + vertex * 16u;
    return float4(asfloat(rcLoad(byteOff)), asfloat(rcLoad(byteOff + 4u)), asfloat(rcLoad(byteOff + 8u)), asfloat(rcLoad(byteOff + 12u)));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectTrace(uint3 dtid : SV_DispatchThreadID) {
    if (gPass.lightProfile < 1u || (gFrame.debugMode != 0u && gFrame.debugMode != 7u)) {
        return;
    }
    const bool half = gPass.lightProfile < 2u;
    const uint2 px = half ? dtid.xy * 2u : dtid.xy;
    if (px.x >= uint(gFrame.extent.x) || px.y >= uint(gFrame.extent.y)) {
        return;
    }
    const uint vis = gVis.Load(int3(px, 0));
    const float depth = gDepth.Load(int3(px, 0));
    if (vis == 0u || depth <= 0.0) {
        if (half) {
            gReflect[int2(dtid.xy)] = 0.0;
        }
        return;
    }
    const float3 P = reconstructWorld(px, depth);
    const float3 N = reconstructNormalFromDepth(gDepth, px, P);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    const float rough = gSurf0[int2(px)].w;
    const float2 u = float2(frac(float(px.x) * 0.754877666), frac(float(px.y) * 0.569840296));
    const float3 dir = gPass.lightProfile >= 2u ? ggxReflect(N, V, rough, u) : reflect(-V, N);
    const float weight = saturate(1.0 - rough);
    float3 col = sampleCube(dir);
    const uint lo = rcLoad(0);
    const uint hi = rcLoad(4);
    if ((lo | hi) != 0u && weight > 0.02) {
        RaytracingAccelerationStructure tlas = RaytracingAccelerationStructure((uint64_t(hi) << 32) | uint64_t(lo));
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        RayDesc ray;
        ray.Origin = P + N * 0.04;
        ray.Direction = dir;
        ray.TMin = 0.0;
        ray.TMax = 48.0;
        q.TraceRayInline(tlas, RAY_FLAG_FORCE_OPAQUE, 0xff, ray);
        q.Proceed();
        if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
            const uint prim = q.CommittedPrimitiveIndex();
            const float3 a = loadPos(prim * 3u).xyz;
            const float3 b = loadPos(prim * 3u + 1u).xyz;
            const float3 c = loadPos(prim * 3u + 2u).xyz;
            float3 n = cross(b - a, c - a);
            n = dot(n, n) > 1e-12 ? normalize(n) : -dir;
            const float3 sun = normalize(gSun.sunDirIntensity.xyz);
            const float ndl = saturate(dot(n, sun));
            col = gSun.sunColor.rgb * gSun.sunDirIntensity.w * ndl * 0.35;
        }
    }
    if (gFrame.debugMode == 7u) {
        gHdr[int2(px)] = float4(col, 1.0);
        return;
    }
    if (half) {
        gReflect[int2(dtid.xy)] = float4(col, weight);
        return;
    }
    gHdr[int2(px)] += float4(col * weight, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void reflectUp(uint3 dtid : SV_DispatchThreadID) {
    if (gPass.lightProfile != 1u) {
        return;
    }
    if (dtid.x >= uint(gFrame.extent.x) || dtid.y >= uint(gFrame.extent.y)) {
        return;
    }
    const float depth = gDepth.Load(int3(dtid.xy, 0));
    const int2 halfPx = int2(dtid.xy) / 2;
    float4 acc = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 s = halfPx + int2(x, y);
            const float4 tap = gReflect[s];
            const float2 full = float2(s * 2);
            const float sd = gDepth.Load(int3(int2(full), 0));
            const float w = exp(-abs(sd - depth) * 40.0);
            acc += tap * w;
            wsum += w;
        }
    }
    const float4 tap = wsum > 1e-4 ? acc / wsum : 0.0;
    if (gFrame.debugMode == 7u) {
        gHdr[int2(dtid.xy)] = float4(tap.rgb, 1.0);
        return;
    }
    gHdr[int2(dtid.xy)] += float4(tap.rgb * tap.a, 0.0);
}

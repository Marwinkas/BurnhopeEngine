// ReSTIR DI: резервуар 8 байт, один кандидат из кластера, один сосед,
// теневой луч через пиксель. Солнце этим проходом не пишется.

#include "frame.slang"
#include "camera.slang"
#include "core/brdf.slang"

[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(4, 0)]] RWTexture2D<float4> gSurf0;
[[vk::binding(5, 0)]] RWTexture2D<float4> gSurf1;
[[vk::binding(11, 0)]] RWStructuredBuffer<uint2> gRes;
[[vk::binding(14, 0)]] RWStructuredBuffer<uint2> gRis;
[[vk::binding(19, 0)]] StructuredBuffer<LightGpu> gLights;
[[vk::binding(22, 0)]] StructuredBuffer<uint4> gCells;
[[vk::binding(23, 0)]] StructuredBuffer<uint> gIds;
[[vk::binding(26, 0)]] RWTexture2D<float4> gRaw;
[[vk::binding(28, 0)]] RWTexture2D<float4> gHist;
[[vk::binding(60, 0)]] ConstantBuffer<GradeBlock> gBlock;

struct LightGpu {
    float4 posRadius;
    float4 colorIntensity;
    float4 dirAngle;
    uint4 meta;
};

struct FastPush {
    uint pass;
    uint hist;
    uint tlasLo;
    uint tlasHi;
};

[[vk::push_constant]]
FastPush gPush;

static const uint kEmptyLight = 0xffffu;
static const float2 kPoisson[8] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379)
};

struct FastRes {
    uint light;
    uint m;
    float wSum;
    float W;
};

float fastIgn(int2 p) {
    return frac(52.9829189 * frac(0.06711056 * float(p.x) + 0.00583715 * float(p.y)));
}

float fastRand(inout uint rng) {
    rng = rng * 747796405u + 2891336453u;
    uint word = ((rng >> ((rng >> 28u) + 4u)) ^ rng) * 277803737u;
    word = (word >> 22u) ^ word;
    return float(word) * (1.0 / 4294967296.0);
}

uint2 fastAt(uint slice, uint2 px) {
    return uint2(px.x, px.y + slice * gFrame.extent.y);
}

uint fastIndex(uint slice, uint2 px) {
    return slice * gFrame.extent.x * gFrame.extent.y + px.y * gFrame.extent.x + px.x;
}

FastRes fastLoad(uint slice, uint2 px) {
    uint2 packed = gRes[fastIndex(slice, px)];
    FastRes r;
    r.light = packed.x & 0xffffu;
    r.m = packed.x >> 16u;
    r.wSum = f16tof32(packed.y & 0xffffu);
    r.W = f16tof32(packed.y >> 16u);
    return r;
}

void fastStore(uint slice, uint2 px, FastRes r) {
    uint m = min(r.m, 32u);
    uint light = r.light > 0xfffeu ? kEmptyLight : r.light;
    gRes[fastIndex(slice, px)] = uint2(light | (m << 16u), f32tof16(max(r.wSum, 0.0)) | (f32tof16(max(r.W, 0.0)) << 16u));
}

bool fastSurface(uint2 px, out float3 pos, out float3 n, out float3 albedo, out float metal, out float viewZ) {
    pos = 0.0;
    n = float3(0.0, 1.0, 0.0);
    albedo = 0.0;
    metal = 0.0;
    viewZ = 0.0;
    const float depth = gDepth.Load(int3(px, 0));
    if (depth <= 0.0) {
        return false;
    }
    pos = reconstructWorld(px, depth);
    n = octDecode(gSurf1[px].xy);
    if (dot(n, n) < 0.5) {
        n = float3(0.0, 1.0, 0.0);
    }
    n = normalize(n);
    float3 view = gFrame.cameraPos.xyz - pos;
    if (dot(n, view) < 0.0) {
        n = -n;
    }
    albedo = max(gSurf0[px].rgb, 0.0);
    metal = saturate(gSurf1[px].z);
    viewZ = max(length(pos - gFrame.cameraPos.xyz), 1e-3);
    return true;
}

uint fastCell(float3 pos, float2 uv) {
    const float nearZ = max(gFrame.cameraPos.w, 0.05);
    const float farZ = max(gSun.sunSplit.z, nearZ + 1.0);
    const float viewZ = max(worldToClip(pos).w, nearZ);
    const float zNorm = log(viewZ / nearZ) / log(farZ / nearZ);
    const uint x = min(uint(saturate(uv.x) * 16.0), 15u);
    const uint y = min(uint(saturate(uv.y) * 9.0), 8u);
    const uint z = min(uint(saturate(zNorm) * 24.0), 23u);
    return x + y * 16u + z * 144u;
}

float fastTarget(float3 pos, float3 n, float3 albedo, float metal, uint light, out float dist, out float3 L) {
    dist = 0.0;
    L = float3(0.0, 1.0, 0.0);
    if (light >= kEmptyLight) {
        return 0.0;
    }
    const LightGpu lamp = gLights[light];
    if (lamp.meta.x == 0u) {
        return 0.0;
    }
    const float3 toL = lamp.posRadius.xyz - pos;
    dist = length(toL);
    const float radius = lamp.posRadius.w;
    if (dist < 1e-3 || dist >= radius) {
        return 0.0;
    }
    L = toL / dist;
    const float ndl = saturate(dot(n, L));
    if (ndl <= 0.0) {
        return 0.0;
    }
    const float atten = filamentDistanceAttenuation(dist, radius);
    const float3 diffuse = albedo * (1.0 - metal) * ndl * atten * lamp.colorIntensity.rgb * lamp.colorIntensity.w;
    return dot(max(diffuse, 0.0), float3(0.2126, 0.7152, 0.0722));
}

float3 fastLe(float3 pos, float3 n, float3 albedo, float metal, uint light) {
    if (light >= kEmptyLight) {
        return 0.0;
    }
    const LightGpu lamp = gLights[light];
    const float3 toL = lamp.posRadius.xyz - pos;
    const float dist = length(toL);
    const float radius = lamp.posRadius.w;
    if (lamp.meta.x == 0u || dist < 1e-3 || dist >= radius) {
        return 0.0;
    }
    const float3 L = toL / dist;
    const float ndl = saturate(dot(n, L));
    const float atten = filamentDistanceAttenuation(dist, radius);
    return max(albedo * (1.0 - metal) * ndl * atten * lamp.colorIntensity.rgb * lamp.colorIntensity.w, 0.0);
}

void fastInitial(uint2 px) {
    const uint cur = 1u;
    FastRes r;
    r.light = kEmptyLight;
    r.m = 0u;
    r.wSum = 0.0;
    r.W = 0.0;
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    if (fastSurface(px, pos, n, albedo, metal, viewZ)) {
        const float2 uv = (float2(px) + 0.5) * gFrame.invExtent;
        const uint4 cell = gCells[fastCell(pos, uv)];
        const uint count = cell.y;
        if (count > 0u) {
            uint rng = px.x * 1973u ^ px.y * 9277u ^ gPush.hist * 26699u;
            const uint pick = min(uint(fastRand(rng) * float(count)), count - 1u);
            const uint light = gIds[cell.x + pick];
            float dist;
            float3 L;
            const float target = fastTarget(pos, n, albedo, metal, light, dist, L);
            if (target > 1e-6) {
                const float proposal = 1.0 / float(count);
                r.light = light;
                r.m = 1u;
                r.wSum = target / proposal;
                r.W = r.wSum / target;
            }
        }
    }
    fastStore(cur, px, r);
}

float4x4 fastPrevVp() {
    return float4x4(
        float4(gBlock.prev0.x, gBlock.prev1.x, gBlock.prev2.x, gBlock.prev3.x),
        float4(gBlock.prev0.y, gBlock.prev1.y, gBlock.prev2.y, gBlock.prev3.y),
        float4(gBlock.prev0.z, gBlock.prev1.z, gBlock.prev2.z, gBlock.prev3.z),
        float4(gBlock.prev0.w, gBlock.prev1.w, gBlock.prev2.w, gBlock.prev3.w));
}

int2 fastPrevPx(float3 pos) {
    const float4 clip = mul(fastPrevVp(), float4(pos, 1.0));
    const float2 uv = clip.xy / max(clip.w, 1e-5) * 0.5 + 0.5;
    if (uv.x <= 0.0 || uv.y <= 0.0 || uv.x >= 1.0 || uv.y >= 1.0) {
        return int2(-1, -1);
    }
    return int2(uv * float2(gFrame.extent.xy));
}

void fastTemporal(uint2 px) {
    const uint cur = 1u;
    const uint hist = 0u;
    FastRes now = fastLoad(cur, px);
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    if (!fastSurface(px, pos, n, albedo, metal, viewZ)) {
        fastStore(cur, px, now);
        return;
    }
    const int2 prev = fastPrevPx(pos);
    if (prev.x < 0) {
        fastStore(cur, px, now);
        return;
    }
    float3 prevPos;
    float3 prevN;
    float3 prevAlbedo;
    float prevMetal;
    float prevZ;
    if (!fastSurface(uint2(prev), prevPos, prevN, prevAlbedo, prevMetal, prevZ)) {
        fastStore(cur, px, now);
        return;
    }
    const float3 toCam = gFrame.cameraPos.xyz - pos;
    const float facing = saturate(dot(n, dot(toCam, toCam) > 1e-8 ? normalize(toCam) : n));
    const float depthTol = lerp(0.06, 0.02, facing);
    if (abs(viewZ - prevZ) > viewZ * depthTol || dot(n, prevN) < 0.9) {
        fastStore(cur, px, now);
        return;
    }
    FastRes old = fastLoad(hist, uint2(prev));
    if (old.m == 0u || old.light >= kEmptyLight) {
        fastStore(cur, px, now);
        return;
    }
    float distNow;
    float3 Lnow;
    const float targetNow = fastTarget(pos, n, albedo, metal, old.light, distNow, Lnow);
    float distPrev;
    float3 Lprev;
    const float targetPrev = fastTarget(prevPos, prevN, prevAlbedo, prevMetal, old.light, distPrev, Lprev);
    float jac = 1.0;
    if (distNow > 1e-3 && distPrev > 1e-3) {
        jac = clamp((distPrev * distPrev) / (distNow * distNow), 0.0, 4.0);
    }
    const float wPrev = targetNow * old.W * float(old.m) * jac;
    if (wPrev <= 1e-6) {
        fastStore(cur, px, now);
        return;
    }
    uint rng = px.x * 9157u ^ px.y * 3719u ^ hist * 199u;
    const float u = fastRand(rng);
    now.wSum += wPrev;
    if (u * now.wSum < wPrev) {
        now.light = old.light;
    }
    now.m = min(now.m + old.m, 32u);
    float chosenDist;
    float3 chosenL;
    const float chosen = fastTarget(pos, n, albedo, metal, now.light, chosenDist, chosenL);
    now.W = chosen > 1e-6 ? now.wSum / (float(now.m) * chosen) : 0.0;
    fastStore(cur, px, now);
}

void fastSpatial(uint2 px) {
    const uint cur = 1u;
    const uint hist = 0u;
    FastRes now = fastLoad(cur, px);
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    if (!fastSurface(px, pos, n, albedo, metal, viewZ)) {
        fastStore(hist, px, now);
        return;
    }
    const float ang = fastIgn(int2(px)) * 6.2831853;
    const float radius = lerp(4.0, 8.0, fastIgn(int2(px.y, px.x)));
    const int2 nb = int2(px) + int2(round(float2(cos(ang), sin(ang)) * radius));
    if (nb.x < 0 || nb.y < 0 || nb.x >= int(gFrame.extent.x) || nb.y >= int(gFrame.extent.y)) {
        fastStore(hist, px, now);
        return;
    }
    float3 nbPos;
    float3 nbN;
    float3 nbAlbedo;
    float nbMetal;
    float nbZ;
    if (!fastSurface(uint2(nb), nbPos, nbN, nbAlbedo, nbMetal, nbZ) || abs(viewZ - nbZ) > viewZ * 0.02 || dot(n, nbN) < 0.9) {
        fastStore(hist, px, now);
        return;
    }
    FastRes other = fastLoad(cur, uint2(nb));
    if (other.m == 0u || other.light >= kEmptyLight) {
        fastStore(hist, px, now);
        return;
    }
    float distNow;
    float3 Lnow;
    const float targetNow = fastTarget(pos, n, albedo, metal, other.light, distNow, Lnow);
    float distNb;
    float3 Lnb;
    const float targetNb = fastTarget(nbPos, nbN, nbAlbedo, nbMetal, other.light, distNb, Lnb);
    float jac = 1.0;
    if (distNow > 1e-3 && distNb > 1e-3) {
        jac = clamp((distNb * distNb) / (distNow * distNow), 0.0, 4.0);
    }
    const float wNb = targetNow * other.W * float(other.m) * jac;
    if (wNb > 1e-6) {
        uint rng = px.x * 433u ^ px.y * 811u;
        if (fastRand(rng) * (now.wSum + wNb) < wNb) {
            now.light = other.light;
        }
        now.wSum += wNb;
        now.m = min(now.m + other.m, 32u);
        float chosenDist;
        float3 chosenL;
        const float chosen = fastTarget(pos, n, albedo, metal, now.light, chosenDist, chosenL);
        now.W = chosen > 1e-6 ? now.wSum / (float(now.m) * chosen) : 0.0;
    }
    fastStore(hist, px, now);
}

RaytracingAccelerationStructure fastScene() {
    return RaytracingAccelerationStructure(uint64_t(gPush.tlasLo) | (uint64_t(gPush.tlasHi) << 32));
}

void fastTrace(uint2 px) {
    const bool active = ((px.x + px.y) & 1u) == (gPush.hist & 1u);
    float vis = 1.0;
    float blocker = -1.0;
    if (active && (gPush.tlasLo != 0u || gPush.tlasHi != 0u)) {
        float3 pos;
        float3 n;
        float3 albedo;
        float metal;
        float viewZ;
        FastRes r = fastLoad(0u, px);
        float dist;
        float3 L;
        if (fastSurface(px, pos, n, albedo, metal, viewZ) && fastTarget(pos, n, albedo, metal, r.light, dist, L) > 1e-6) {
            RayDesc ray;
            ray.Origin = pos + n * 0.02;
            ray.Direction = L;
            ray.TMin = 0.02;
            ray.TMax = max(dist - 0.02, 0.02);
            uint ignored;
            InterlockedAdd(gRis[1024u * 128u + 7u].x, 1u, ignored);
            RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
            query.TraceRayInline(fastScene(), RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER, 0xff, ray);
            query.Proceed();
            if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
                vis = 0.0;
                blocker = max(dist - query.CommittedRayT(), 0.02);
            } else {
                blocker = dist;
            }
            gRaw[px] = float4(vis, blocker, dist, 1.0);
            return;
        }
    }
    gRaw[px] = float4(vis, blocker, 0.0, active ? 1.0 : 0.0);
}

float4 fastRebuild(uint2 px) {
    float4 raw = gRaw[px];
    if (raw.g >= 0.0) {
        return raw;
    }
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    const bool live = fastSurface(px, pos, n, albedo, metal, viewZ);
    const int2 cross[4] = { int2(1, 0), int2(-1, 0), int2(0, 1), int2(0, -1) };
    float vis = 0.0;
    float blocker = 0.0;
    float reach = 0.0;
    float weight = 0.0;
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        const int2 q = int2(px) + cross[i];
        if (q.x < 0 || q.y < 0 || q.x >= int(gFrame.extent.x) || q.y >= int(gFrame.extent.y)) {
            continue;
        }
        const float4 nb = gRaw[uint2(q)];
        if (nb.g < 0.0) {
            continue;
        }
        float w = 0.0;
        if (live) {
            float3 qPos;
            float3 qN;
            float3 qAlbedo;
            float qMetal;
            float qZ;
            if (fastSurface(uint2(q), qPos, qN, qAlbedo, qMetal, qZ)) {
                const float nd = saturate(dot(n, qN));
                const float depthW = exp(-abs(viewZ - qZ) / max(viewZ * 0.02, 1e-3));
                w = depthW * pow(nd, 32.0);
            }
        }
        if (w <= 1e-4) {
            continue;
        }
        vis += nb.r * w;
        blocker += nb.g * w;
        reach += nb.b * w;
        weight += w;
    }
    if (weight > 1e-4) {
        raw = float4(vis / weight, blocker / weight, reach / weight, 1.0);
    }
    return raw;
}

void fastTemporalVis(uint2 px) {
    const float4 cur = fastRebuild(px);
    float vis = saturate(cur.r);
    float mean = 0.0;
    float moment = 0.0;
    float count = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 q = int2(px) + int2(x, y);
            if (q.x < 0 || q.y < 0 || q.x >= int(gFrame.extent.x) || q.y >= int(gFrame.extent.y)) {
                continue;
            }
            const float4 nb = gRaw[uint2(q)];
            if (nb.g < 0.0) {
                continue;
            }
            const float s = saturate(nb.r);
            mean += s;
            moment += s * s;
            count += 1.0;
        }
    }
    float dev = 0.0;
    if (count > 0.0) {
        mean /= count;
        dev = sqrt(max(moment / count - mean * mean, 0.0));
    }
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    const uint histSlice = 2u + (gPush.hist & 1u);
    const uint destSlice = 2u + ((gPush.hist ^ 1u) & 1u);
    float histLen = 1.0;
    if (fastSurface(px, pos, n, albedo, metal, viewZ)) {
        const int2 prev = fastPrevPx(pos);
        if (prev.x >= 0 && prev.x < int(gFrame.extent.x) && prev.y >= 0 && prev.y < int(gFrame.extent.y)) {
            const float4 hist = gHist[fastAt(histSlice, uint2(prev))];
            float3 prevPos;
            float3 prevN;
            float3 prevAlbedo;
            float prevMetal;
            float prevZ;
            const bool prevLive = fastSurface(uint2(prev), prevPos, prevN, prevAlbedo, prevMetal, prevZ);
            const float3 toCam = gFrame.cameraPos.xyz - pos;
            const float facing = saturate(dot(n, dot(toCam, toCam) > 1e-8 ? normalize(toCam) : n));
            const float depthTol = lerp(0.06, 0.02, facing);
            const float dz = abs(viewZ - prevZ) / max(viewZ, 1e-3);
            if (hist.a >= 1.0 && prevLive && dot(n, prevN) >= 0.95 && dz <= depthTol) {
                histLen = clamp(hist.a, 1.0, 8.0);
                const float kept = saturate(hist.r);
                const float weight = histLen / (histLen + 1.0);
                vis = lerp(vis, kept, weight);
                histLen = min(histLen + 1.0, 8.0);
            }
        }
    }
    const float band = max(dev * 2.0, 0.12);
    vis = count > 0.0 ? clamp(vis, mean - band, mean + band) : vis;
    gHist[fastAt(destSlice, px)] = float4(vis, cur.g, cur.b, histLen);
    gRaw[fastAt(1u, px)] = float4(vis, cur.g, cur.b, histLen);
}

void fastBlur(uint2 px) {
    const float4 cur = gRaw[fastAt(1u, px)];
    float vis = saturate(cur.r);
    float3 pos;
    float3 n;
    float3 albedo;
    float metal;
    float viewZ;
    FastRes r = fastLoad(0u, px);
    const bool live = fastSurface(px, pos, n, albedo, metal, viewZ);
    if (live) {
        const int2 ext = int2(gFrame.extent.xy) - int2(1, 1);
        float sum = 0.0;
        float weight = 0.0;
        [unroll]
        for (int y = -1; y <= 1; ++y) {
            [unroll]
            for (int x = -1; x <= 1; ++x) {
                const int2 q = clamp(int2(px) + int2(x, y), int2(0, 0), ext);
                float3 qPos;
                float3 qN;
                float3 qAlbedo;
                float qMetal;
                float qZ;
                if (!fastSurface(uint2(q), qPos, qN, qAlbedo, qMetal, qZ)) {
                    continue;
                }
                const float dz = abs(viewZ - qZ) / max(viewZ * 0.02, 1e-3);
                const float w = pow(saturate(dot(n, qN)), 64.0) * exp(-dz * dz);
                sum += gRaw[fastAt(1u, uint2(q))].r * w;
                weight += w;
            }
        }
        if (weight > 1e-4) {
            vis = saturate(sum / weight);
        }
        if (cur.a < 4.0) {
            const int2 cross[4] = { int2(2, 0), int2(-2, 0), int2(0, 2), int2(0, -2) };
            [unroll]
            for (uint i = 0u; i < 4u; ++i) {
                const int2 q = clamp(int2(px) + cross[i], int2(0, 0), ext);
                float3 qPos;
                float3 qN;
                float3 qAlbedo;
                float qMetal;
                float qZ;
                if (!fastSurface(uint2(q), qPos, qN, qAlbedo, qMetal, qZ)) {
                    continue;
                }
                const float dz = abs(viewZ - qZ) / max(viewZ * 0.02, 1e-3);
                const float w = pow(saturate(dot(n, qN)), 32.0) * exp(-dz * dz) * 0.5;
                sum += gRaw[fastAt(1u, uint2(q))].r * w;
                weight += w;
            }
            if (weight > 1e-4) {
                vis = saturate(sum / weight);
            }
        }
    }
    if (!live) {
        return;
    }
    const uint mode = gFrame.debugMode;
    if (mode != 0u && mode != 8u) {
        return;
    }
    float chosenDist;
    float3 chosenL;
    const float target = fastTarget(pos, n, albedo, metal, r.light, chosenDist, chosenL);
    if (target <= 1e-6 || r.W <= 0.0) {
        return;
    }
    const float3 add = fastLe(pos, n, albedo, metal, r.light) * min(r.W, 8.0) * vis;
    float4 hdr = gHdr[px];
    hdr.rgb += max(add, 0.0);
    gHdr[px] = hdr;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
        return;
    }
    const uint2 px = dtid.xy;
    if (gPush.pass == 0u) {
        fastInitial(px);
    } else if (gPush.pass == 1u) {
        fastTemporal(px);
    } else if (gPush.pass == 2u) {
        fastSpatial(px);
    } else if (gPush.pass == 3u) {
        fastTrace(px);
    } else if (gPush.pass == 4u) {
        fastTemporalVis(px);
    } else {
        fastBlur(px);
    }
}

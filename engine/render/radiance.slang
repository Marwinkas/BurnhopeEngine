#include "frame.slang"
#include "camera.slang"

// Sparse hash of world cells. A ray from a visible surface is split by distance
// into cascade intervals t_n = spacing * 4^n. The hit is stored on the cell of
// the surface, not the screen, so the light stays put when the camera turns.

struct Addr {
    uint64_t asAddr;
};

[[vk::binding(30, 0)]] RWByteAddressBuffer gRc;
[[vk::binding(1, 0)]] Texture2D<uint> gVis;
[[vk::binding(2, 0)]] Texture2D<float> gDepth;
[[vk::binding(3, 0)]] RWTexture2D<float4> gHdr;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstances;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;

float3 receiverNormal(uint2 px, float3 P) {
    const uint2 ext = gFrame.extent.xy;
    const uint2 ax = uint2(min(px.x + 1u, ext.x - 1u), px.y);
    const uint2 ay = uint2(px.x, min(px.y + 1u, ext.y - 1u));
    const float3 ex = reconstructWorld(ax, gDepth.Load(int3(ax, 0))) - P;
    const float3 ey = reconstructWorld(ay, gDepth.Load(int3(ay, 0))) - P;
    float3 n = cross(ex, ey);
    n = dot(n, n) > 1e-12 ? normalize(n) : float3(0.0, 1.0, 0.0);
    const float3 V = normalize(gFrame.cameraPos.xyz - P);
    if (dot(n, V) < 0.0) {
        n = -n;
    }
    return n;
}

uint rcLoad(uint byteOff) {
    return gRc.Load(byteOff);
}

void rcStore(uint byteOff, uint value) {
    gRc.Store(byteOff, value);
}

uint slotCount() {
    return max(rcLoad(8), 1u);
}

uint hashCell(int3 cell, uint cascade) {
    uint h = uint(cell.x) * 73856093u ^ uint(cell.y) * 19349663u ^ uint(cell.z) * 83492791u;
    h ^= cascade * 16777619u;
    return h | 1u;
}

uint slotByte(uint index) {
    return 32u + (index & (slotCount() - 1u)) * 32u;
}

bool lookup(int3 cell, uint cascade, out float3 rad, out float trans, out uint count) {
    const uint key = hashCell(cell, cascade);
    const uint n = slotCount();
    const uint h = key & (n - 1u);
    rad = 0.0;
    trans = 1.0;
    count = 0u;
    [loop]
    for (uint i = 0; i < 8u; ++i) {
        const uint byteOff = slotByte(h + i);
        if (rcLoad(byteOff) != key) {
            continue;
        }
        count = rcLoad(byteOff + 4u);
        if (count == 0u) {
            return false;
        }
        const float s = 1.0 / float(count);
        rad = float3(rcLoad(byteOff + 8u), rcLoad(byteOff + 12u), rcLoad(byteOff + 16u)) * s * (1.0 / 8.0);
        trans = float(rcLoad(byteOff + 20u)) * s * (1.0 / 256.0);
        return true;
    }
    return false;
}

void deposit(int3 cell, uint cascade, float3 rad, float trans) {
    const uint key = hashCell(cell, cascade);
    const uint n = slotCount();
    const uint h = key & (n - 1u);
    [loop]
    for (uint i = 0; i < 8u; ++i) {
        const uint byteOff = slotByte(h + i);
        uint prev = 0u;
        gRc.InterlockedCompareExchange(byteOff, 0u, key, prev);
        if (prev != 0u && prev != key) {
            continue;
        }
        gRc.InterlockedAdd(byteOff + 4u, 1u);
        gRc.InterlockedAdd(byteOff + 8u, uint(min(rad.r, 32.0) * 8.0));
        gRc.InterlockedAdd(byteOff + 12u, uint(min(rad.g, 32.0) * 8.0));
        gRc.InterlockedAdd(byteOff + 16u, uint(min(rad.b, 32.0) * 8.0));
        gRc.InterlockedAdd(byteOff + 20u, uint(saturate(trans) * 256.0));
        return;
    }
}

float4 loadPos(uint vertex) {
    const uint byteOff = rcLoad(12) + vertex * 16u;
    return float4(asfloat(rcLoad(byteOff)), asfloat(rcLoad(byteOff + 4u)), asfloat(rcLoad(byteOff + 8u)), asfloat(rcLoad(byteOff + 12u)));
}

float3 hemisphere(uint index, float3 N) {
    const float2 uv = frac(float2(float(index) * 0.754877666, float(index) * 0.569840296));
    const float phi = uv.x * 6.2831853;
    const float z = uv.y;
    const float r = sqrt(max(1.0 - z * z, 0.0));
    const float3 up = abs(N.y) > 0.98 ? float3(1.0, 0.0, 0.0) : float3(0.0, 1.0, 0.0);
    const float3 T = normalize(cross(up, N));
    const float3 B = cross(N, T);
    return normalize(T * (cos(phi) * r) + B * (sin(phi) * r) + N * z);
}

int3 cellAt(float3 world, uint cascade) {
    const float sp = max(gRcParams.spacing, 0.05) * exp2(float(cascade));
    return int3(floor(world / sp));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcTrace(uint3 dtid : SV_DispatchThreadID) {
    const uint2 px = dtid.xy * 2u;
    if (px.x >= gFrame.extent.x || px.y >= gFrame.extent.y) {
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
    const float3 N = receiverNormal(px, P);
    RaytracingAccelerationStructure tlas = RaytracingAccelerationStructure((uint64_t(rcLoad(4)) << 32) | uint64_t(rcLoad(0)));
    const uint rays = uint(clamp(int(gRcParams.rays), 1, 32));
    const float reach = max(gRcParams.maxDist, 1.0);
    const float3 sun = normalize(gFrame.sunDirIntensity.xyz);
    const float3 sunCol = gFrame.sunColor.rgb * gFrame.sunDirIntensity.w;
    [loop]
    for (uint r = 0u; r < rays; ++r) {
        const float3 dir = hemisphere(px.x * 17u + px.y * 131u + r * 64u, N);
        RayQuery<RAY_FLAG_FORCE_OPAQUE> q;
        RayDesc ray;
        ray.Origin = P + dir * 0.05;
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
            const float ndl = saturate(dot(n, sun));
            Le = albedo * sunCol * ndl + albedo * gMat[matId].emissive;
        } else {
            Le = float3(0.42, 0.52, 0.72) * saturate(dir.y * 0.6 + 0.35);
            hitT = reach;
        }
        const float base = max(gRcParams.spacing, 0.05);
        uint cascade = 3u;
        [unroll]
        for (uint n = 0u; n < 4u; ++n) {
            const float tend = base * exp2(float(n) * 2.0);
            if (hitT <= tend) {
                cascade = n;
                break;
            }
        }
        deposit(cellAt(P, cascade), cascade, Le, 0.0);
    }
}

float3 irradianceAt(float3 world) {
    float3 acc = 0.0;
    float weight = 0.0;
    [unroll]
    for (uint n = 0u; n < 4u; ++n) {
        float3 J = 0.0;
        float beta = 1.0;
        uint count = 0u;
        if (lookup(cellAt(world, n), n, J, beta, count) && count > 0u) {
            acc += J * float(count);
            weight += float(count);
        }
    }
    const float3 sky = float3(0.04, 0.05, 0.07);
    return (weight > 0.0 ? acc / weight : sky) * 3.14159265;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void rcApply(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= gFrame.extent.x || dtid.y >= gFrame.extent.y) {
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
    const uint packed = vis - 1u;
    const MaterialGpu mat = gMat[gInstances[packed & 0xFFFFFu].materialId];
    const float3 gi = min(irradianceAt(P), gRcParams.cap) * max(gRcParams.intensity, 0.0);
    if (gFrame.debugMode == 4) {
        gHdr[px] = float4(gi, 1.0);
        return;
    }
    float4 hdr = gHdr[px];
    hdr.rgb += mat.baseColor.rgb * gi;
    gHdr[px] = hdr;
}

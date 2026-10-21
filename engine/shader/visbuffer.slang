// visbuffer — one meshlet per group. 64 vertices, 124 triangles.
// Pixel: 20b instance | 12b primitive inside the meshlet. Zero means no surface.

#include "frame.slang"
#include "camera.slang"
#include "core/math.slang"

#ifndef LATE
#define LATE 0
#endif

[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstanceBuf;
[[vk::binding(11, 0)]] StructuredBuffer<uint> gVisible;
// >= 0 выбирает сборку shadowMesh. Номер каскада приходит в gPush.cascade, не в дефайн.
#ifndef SHADOW_CASCADE
#define SHADOW_CASCADE -1
#endif
#if SHADOW_CASCADE >= 0
[[vk::binding(18, 0)]] StructuredBuffer<uint> gShadowVisible;
#endif
#ifndef POINT_LIGHT
#define POINT_LIGHT -1
#endif
#if POINT_LIGHT >= 0
[[vk::binding(19, 0)]] StructuredBuffer<uint> gPointVisible;
#endif

float3 instancePoint(GpuInstance inst, float3 local) {
    const float4 h = float4(local, 1.0);
    return float3(dot(inst.world0, h), dot(inst.world1, h), dot(inst.world2, h));
}

struct VertOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint instanceId : COLOR0;
    nointerpolation float fade : COLOR2;
    nointerpolation uint coarse : COLOR3;
};

[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(24, 0)]] SamplerState gWrap;
float lodPixels(GpuInstance inst) {
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    float3 center = inst.sphere.xyz;
    float radius = inst.sphere.w;
    if (meshlet.groupSphere.w > 1e-4) {
        const float4 h = float4(meshlet.groupSphere.xyz, 1.0);
        const float sx = length(inst.world0.xyz);
        const float sy = length(inst.world1.xyz);
        const float sz = length(inst.world2.xyz);
        radius = meshlet.groupSphere.w * max(sx, max(sy, sz));
        center = float3(dot(inst.world0, h), dot(inst.world1, h), dot(inst.world2, h));
    }
    const float4 clip = worldToClip(center);
    const float z = max(abs(clip.w), 0.1);
    const float4 up = mul(gFrame.viewProj, float4(center + float3(0.0, radius, 0.0), 1.0));
    const float2 d = up.xy / max(abs(up.w), 0.1) - clip.xy / z;
    return length(d) * float(gFrame.extent.y) * 0.5;
}

float lodFade(GpuInstance inst) {
    (void)inst;
    return 1.0;
}

uint lodCoarse(GpuInstance inst) {
    (void)inst;
    return 0u;
}

bool instanceFlippedWinding(GpuInstance inst) {
    const float3 c = cross(inst.world0.xyz, inst.world1.xyz);
    return dot(c, inst.world2.xyz) < 0.0;
}


struct PrimOut {
    nointerpolation uint primId : COLOR1;
};

uint packVis(uint instanceId, uint primitiveId) {
    return ((instanceId & 0xFFFFF) | ((primitiveId & 0xFFF) << 20)) + 1;
}

groupshared float4 gClip[64];
groupshared uint gKept;

float2 clipPixel(float4 c) {
    const float w = max(abs(c.w), 1e-6);
    const float2 ndc = c.xy / w;
    return (ndc * 0.5 + 0.5) * float2(gFrame.extent.xy);
}

bool clipFinite(float4 c) {
    return !isnan(c.x) && !isnan(c.y) && !isnan(c.z) && !isnan(c.w)
        && abs(c.x) < 1e8 && abs(c.y) < 1e8 && abs(c.z) < 1e8 && abs(c.w) < 1e8;
}

bool triKeep(uint3 tri, uint vertCount, bool dropSmall, bool twoSided, bool flippedWinding) {
    if (tri.x >= vertCount || tri.y >= vertCount || tri.z >= vertCount) {
        return false;
    }
    const float4 a = gClip[tri.x];
    const float4 b = gClip[tri.y];
    const float4 c = gClip[tri.z];
    if (!clipFinite(a) || !clipFinite(b) || !clipFinite(c)) {
        return false;
    }
    if (a.w <= 1e-4 && b.w <= 1e-4 && c.w <= 1e-4) {
        return false;
    }
    const bool inFront = a.w > 1e-4 && b.w > 1e-4 && c.w > 1e-4;
    if (!inFront) {
        return true;
    }
    const float2 sa = clipPixel(a);
    const float2 sb = clipPixel(b);
    const float2 sc = clipPixel(c);
    const float area = (sb.x - sa.x) * (sc.y - sa.y) - (sb.y - sa.y) * (sc.x - sa.x);
    if (abs(area) < 1e-7) {
        return false;
    }
    // Backface culling только для основной камеры (dropSmall == true) и с учетом знака определителя матрицы инстанса
    if (dropSmall && gFrame.disableConeCull == 0 && !twoSided) {
        const bool backfacing = flippedWinding ? (area <= 0.0) : (area >= 0.0);
        if (backfacing) {
            return false;
        }
    }
    const float minx = min(sa.x, min(sb.x, sc.x));
    const float maxx = max(sa.x, max(sb.x, sc.x));
    const float miny = min(sa.y, min(sb.y, sc.y));
    const float maxy = max(sa.y, max(sb.y, sc.y));
    if (dropSmall) {
        const float2 ext = float2(gFrame.extent.xy);
        if (maxx < 0.0 || minx >= ext.x || maxy < 0.0 || miny >= ext.y) {
            return false;
        }
    }
    if (gFrame.disableSubpixel != 0 || !dropSmall || a.w <= gFrame.cameraPos.w || b.w <= gFrame.cameraPos.w || c.w <= gFrame.cameraPos.w) {
        return true;
    }
    // Консервативный допуск 1/256 пикселя под фиксированную сетку растеризатора GPU
    const float eps = 1.0 / 256.0;
    const float ix0 = ceil((minx - eps) - 0.5);
    const float ix1 = floor((maxx + eps) - 0.5);
    const float iy0 = ceil((miny - eps) - 0.5);
    const float iy1 = floor((maxy + eps) - 0.5);
    return ix0 <= ix1 && iy0 <= iy1;
}
uint meshletSlot(bool keep) {
    const uint count = WaveActiveCountBits(keep);
    const uint prefix = WavePrefixCountBits(keep);
    uint base = 0;
    if (WaveIsFirstLane() && count > 0u) {
        InterlockedAdd(gKept, count, base);
    }
    base = WaveReadLaneFirst(base);
    return base + prefix;
}

#if SHADOW_CASCADE >= 0
struct ShadowPush {
    uint cascade;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
ShadowPush gPush;
#elif POINT_LIGHT < 0
struct FlightPush {
    uint flight;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
FlightPush gPush;
#endif

void emitMeshlet(uint gtid, uint gid, float4x4 viewProj, bool dropSmall,
    out vertices VertOut verts[64], out indices uint3 prims[124], out primitives PrimOut primPayload[124]) {
#if SHADOW_CASCADE >= 0
    const uint src = gShadowVisible[gPush.cascade * uint(gFrame.padInv.x) + gid];
#elif POINT_LIGHT >= 0
    const uint src = 0u;
#elif LATE
    const uint src = gVisible[gPush.flight * 2u * uint(gFrame.padInv.x) + uint(gFrame.padInv.x) + gid];
#else
    const uint src = gVisible[gPush.flight * 2u * uint(gFrame.padInv.x) + gid];
#endif
    const GpuInstance inst = gInstanceBuf[src];
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint vertCount = min(meshlet.vertexCount, 64);
    const uint triCount = min(meshlet.triangleCount, 124);
    if (gtid == 0) {
        gKept = 0;
    }
    if (gtid < vertCount) {
        const Vertex srcVert = gVerts[meshlet.vertexOffset + gtid];
        const float3 world = instancePoint(inst, srcVert.pos);
        float4 clip = mul(viewProj, float4(world, 1.0));
        gClip[gtid] = clip;
    }
    GroupMemoryBarrierWithGroupSync();
    uint3 tri = uint3(0, 0, 0);
    bool keep = false;
    if (gtid < triCount) {
        const uint at = meshlet.indexOffset + gtid * 3;
        tri = uint3(gIdx[at], gIdx[at + 1], gIdx[at + 2]);
        const bool twoSided = (gMat[inst.materialId].flags & 2u) != 0u;
        keep = triKeep(tri, vertCount, dropSmall, twoSided,instanceFlippedWinding(inst));
    }
    const uint slot = meshletSlot(keep);
    GroupMemoryBarrierWithGroupSync();
    if (gtid == 0) {
        SetMeshOutputCounts(vertCount, gKept);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < vertCount) {
        verts[gtid].pos = gClip[gtid];
        verts[gtid].uv = float2(gVerts[meshlet.vertexOffset + gtid].u, gVerts[meshlet.vertexOffset + gtid].v);
        verts[gtid].instanceId = src;
#if SHADOW_CASCADE >= 0
        verts[gtid].fade = 1.0;
        verts[gtid].coarse = 0;
#else
        verts[gtid].fade = lodFade(inst);
        verts[gtid].coarse = lodCoarse(inst);
#endif
    }
    if (keep) {
        prims[slot] = tri;
        primPayload[slot].primId = gtid;
    }
}

[shader("mesh")]
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void visMesh(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices VertOut verts[64],
    out indices uint3 prims[124],
    out primitives PrimOut primPayload[124])
{
    emitMeshlet(gtid, gid, gFrame.viewProj, true, verts, prims, primPayload);
}

[shader("mesh")]
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void shadowMesh(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices VertOut verts[64],
    out indices uint3 prims[124],
    out primitives PrimOut primPayload[124])
{
#if SHADOW_CASCADE >= 0
    emitMeshlet(gtid, gid, gSun.sunCascade[gPush.cascade], false, verts, prims, primPayload);
#else
    emitMeshlet(gtid, gid, gSun.sunCascade[0], false, verts, prims, primPayload);
#endif
}

#if POINT_LIGHT >= 0
struct PointPush {
    uint light;
    uint pad0;
    uint pad1;
    uint pad2;
};

[[vk::push_constant]]
PointPush gPush;

[shader("mesh")]
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void pointMesh(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices VertOut verts[64],
    out indices uint3 prims[124],
    out primitives PrimOut primPayload[124])
{
    const uint light = gPush.light;
    const float mode = gSun.pad[light];
    const uint face = mode == 0.0 ? 0u : uint(abs(mode) + 0.5) - 1u;
    const uint item = gid;
    const uint src = gPointVisible[light * uint(gFrame.padInv.x) + item];
    const GpuInstance inst = gInstanceBuf[src];
    const MeshletRef meshlet = gMeshlets[inst.meshId];
    const uint vertCount = min(meshlet.vertexCount, 64);
    const uint triCount = min(meshlet.triangleCount, 124);
    if (gtid == 0) {
        gKept = 0;
    }
    if (gtid < vertCount) {
        const Vertex srcVert = gVerts[meshlet.vertexOffset + gtid];
        const float3 world = instancePoint(inst, srcVert.pos);
        const float page = mode < 0.0 ? (64.0 / 512.0) : 1.0;
        float2 local = 0.0;
        float depth01 = 1.0;
        if (mode == 0.0) {
            const float3 fromLight = world - gSun.pointPos[light].xyz;
            const float dist = max(length(fromLight), 1e-4);
            local = octEncode(fromLight / dist) * page;
            depth01 = saturate(dist / max(gSun.pointPos[light].w, 0.05));
        } else {
            float4 clip = mul(gSun.pointFace[light][face], float4(world, 1.0));
            const float w = clip.w == 0.0 ? 1.0 : clip.w;
            local = (clip.xy / w * 0.5 + 0.5) * page;
            depth01 = saturate(clip.z / w);
        }
        const float2 atlas = float2(local.x, (local.y + float(light)) / 3.0);
        gClip[gtid] = float4((atlas * 2.0 - 1.0), depth01, 1.0);
    }
    GroupMemoryBarrierWithGroupSync();
    uint3 tri = uint3(0, 0, 0);
    bool keep = false;
    if (gtid < triCount) {
        const uint at = meshlet.indexOffset + gtid * 3;
        tri = uint3(gIdx[at], gIdx[at + 1], gIdx[at + 2]);
        const bool twoSided = (gMat[inst.materialId].flags & 2u) != 0u;
        keep = triKeep(tri, vertCount, false, twoSided,instanceFlippedWinding(inst));
    }
    const uint slot = meshletSlot(keep);
    GroupMemoryBarrierWithGroupSync();
    if (gtid == 0) {
        SetMeshOutputCounts(vertCount, gKept);
    }
    GroupMemoryBarrierWithGroupSync();
    if (gtid < vertCount) {
        verts[gtid].pos = gClip[gtid];
        verts[gtid].uv = float2(gVerts[meshlet.vertexOffset + gtid].u, gVerts[meshlet.vertexOffset + gtid].v);
        verts[gtid].instanceId = src;
        verts[gtid].fade = 1.0;
        verts[gtid].coarse = 0;
    }
    if (keep) {
        prims[slot] = tri;
        primPayload[slot].primId = gtid;
    }
}
#endif

[shader("fragment")]
void shadowFs(VertOut vin) {
    const GpuInstance inst = gInstanceBuf[vin.instanceId];
    const MaterialGpu mat = gMat[inst.materialId];
    if ((mat.flags & 32u) != 0u && mat.albedo > 0u && mat.albedo < 768u) {
        const float alpha = gTex[NonUniformResourceIndex(mat.albedo)].Sample(gWrap, vin.uv).a;
        if (alpha < 0.5) {
            discard;
        }
    }
}

[shader("fragment")]
uint visFs(VertOut vin, nointerpolation uint primId : COLOR1) : SV_Target0 {
    const float threshold = bayer4(uint2(vin.pos.xy));
    if (vin.fade < 1.0) {
        if (vin.coarse == 0u) {
            if (threshold >= vin.fade) {
                discard;
            }
        } else if (threshold < vin.fade) {
            discard;
        }
    }
    const GpuInstance inst = gInstanceBuf[vin.instanceId];
    const MaterialGpu mat = gMat[inst.materialId];
    if ((mat.flags & 32u) != 0u && mat.albedo > 0u && mat.albedo < 768u) {
        const float alpha = gTex[NonUniformResourceIndex(mat.albedo)].Sample(gWrap, vin.uv).a;
        if (alpha < 0.5) {
            discard;
        }
    }
    return packVis(vin.instanceId, primId);
}

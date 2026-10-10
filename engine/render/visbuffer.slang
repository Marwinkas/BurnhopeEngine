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

float lodFade(GpuInstance inst) {
    if ((inst.pad.y & 0x80000000u) == 0u) {
        return 1.0;
    }
    const float4 clip = worldToClip(inst.sphere.xyz);
    const float px = inst.sphere.w / max(clip.w, 1e-3) * float(gFrame.extent.y) * 2.0;
    const uint lod = inst.pad.x;
    const uint maxLod = ((inst.pad.y >> 28) & 7u) == 0u ? 1u : (inst.pad.y >> 28) & 7u;
    const float lo = lod == 0u ? 28.0 : lod == 1u ? 12.0 : 4.0;
    const float hi = lo + 8.0;
    if (lod < maxLod && px >= lo && px <= hi) {
        return saturate((px - lo) / 8.0);
    }
    if (lod > 0u && px >= (lod == 1u ? 28.0 : lod == 2u ? 12.0 : 4.0) && px <= (lod == 1u ? 36.0 : lod == 2u ? 20.0 : 12.0)) {
        return saturate((px - (lod == 1u ? 28.0 : lod == 2u ? 12.0 : 4.0)) / 8.0);
    }
    return 1.0;
}

uint lodCoarse(GpuInstance inst) {
    if ((inst.pad.y & 0x80000000u) == 0u || inst.pad.x == 0u) {
        return 0u;
    }
    const float4 clip = worldToClip(inst.sphere.xyz);
    const float px = inst.sphere.w / max(clip.w, 1e-3) * float(gFrame.extent.y) * 2.0;
    const uint lod = inst.pad.x;
    const float lo = lod == 1u ? 28.0 : lod == 2u ? 12.0 : 4.0;
    const float hi = lo + 8.0;
    return px >= lo && px <= hi ? 1u : 0u;
}

struct PrimOut {
    nointerpolation uint primId : COLOR1;
};

uint packVis(uint instanceId, uint primitiveId) {
    return ((instanceId & 0xFFFFF) | ((primitiveId & 0xFFF) << 20)) + 1;
}

groupshared float4 gClip[64];
groupshared uint gKept;

float2 clipScreen(float4 c) {
    const float w = max(abs(c.w), 1e-6);
    return (c.xy / w) * float2(gFrame.extent.xy) * 0.5;
}

bool clipFinite(float4 c) {
    return !isnan(c.x) && !isnan(c.y) && !isnan(c.z) && !isnan(c.w)
        && abs(c.x) < 1e8 && abs(c.y) < 1e8 && abs(c.z) < 1e8 && abs(c.w) < 1e8;
}

bool triKeep(uint3 tri, uint vertCount, bool dropSmall) {
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
    const float2 sa = clipScreen(a);
    const float2 sb = clipScreen(b);
    const float2 sc = clipScreen(c);
    const float area = (sb.x - sa.x) * (sc.y - sa.y) - (sb.y - sa.y) * (sc.x - sa.x);
    if (abs(area) < 1e-6) {
        return false;
    }
    if (gFrame.disableSubpixel != 0) {
        return true;
    }
    if (!dropSmall || a.w <= gFrame.cameraPos.w || b.w <= gFrame.cameraPos.w || c.w <= gFrame.cameraPos.w) {
        return true;
    }
    const float minx = min(sa.x, min(sb.x, sc.x));
    const float maxx = max(sa.x, max(sb.x, sc.x));
    const float miny = min(sa.y, min(sb.y, sc.y));
    const float maxy = max(sa.y, max(sb.y, sc.y));
    return (maxx - minx) >= 0.75 || (maxy - miny) >= 0.75;
}

void emitMeshlet(uint gtid, uint gid, float4x4 viewProj, bool dropSmall,
    out vertices VertOut verts[64], out indices uint3 prims[124], out primitives PrimOut primPayload[124]) {
#ifndef CULL_FLIGHT
#define CULL_FLIGHT 0
#endif
#if SHADOW_CASCADE >= 0
    const uint src = gShadowVisible[gid];
#elif LATE
    const uint src = gVisible[CULL_FLIGHT * 2u * uint(gFrame.padInv.x) + uint(gFrame.padInv.x) + gid];
#else
    const uint src = gVisible[CULL_FLIGHT * 2u * uint(gFrame.padInv.x) + gid];
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
        keep = triKeep(tri, vertCount, dropSmall);
    }
    uint slot = 0;
    if (keep) {
        InterlockedAdd(gKept, 1, slot);
    }
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
    emitMeshlet(gtid, gid, gSun.sunCascade[SHADOW_CASCADE], false, verts, prims, primPayload);
}

#if POINT_LIGHT >= 0
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
    const float mode = gSun.pad[POINT_LIGHT];
    const uint face = mode == 0.0 ? 0u : uint(abs(mode) + 0.5) - 1u;
    const uint item = gid;
    const uint src = gPointVisible[item];
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
            const float3 fromLight = world - gSun.pointPos[POINT_LIGHT].xyz;
            const float dist = max(length(fromLight), 1e-4);
            local = octEncode(fromLight / dist) * page;
            depth01 = saturate(dist / max(gSun.pointPos[POINT_LIGHT].w, 0.05));
        } else {
            float4 clip = mul(gSun.pointFace[POINT_LIGHT][face], float4(world, 1.0));
            const float w = clip.w == 0.0 ? 1.0 : clip.w;
            local = (clip.xy / w * 0.5 + 0.5) * page;
            depth01 = saturate(clip.z / w);
        }
        const float2 atlas = float2(local.x, (local.y + float(POINT_LIGHT)) / 3.0);
        gClip[gtid] = float4((atlas * 2.0 - 1.0), depth01, 1.0);
    }
    GroupMemoryBarrierWithGroupSync();
    uint3 tri = uint3(0, 0, 0);
    bool keep = false;
    if (gtid < triCount) {
        const uint at = meshlet.indexOffset + gtid * 3;
        tri = uint3(gIdx[at], gIdx[at + 1], gIdx[at + 2]);
        keep = triKeep(tri, vertCount, false);
    }
    uint slot = 0;
    if (keep) {
        InterlockedAdd(gKept, 1, slot);
    }
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
    if (mat.albedo > 0u && mat.albedo < 768u) {
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
    if (mat.albedo > 0u && mat.albedo < 768u) {
        const float alpha = gTex[NonUniformResourceIndex(mat.albedo)].Sample(gWrap, vin.uv).a;
        if (alpha < 0.5) {
            discard;
        }
    }
    return packVis(vin.instanceId, primId);
}

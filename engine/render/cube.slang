#include "frame.slang"

[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;
[[vk::binding(8, 0)]] StructuredBuffer<uint> gIdx;
[[vk::binding(9, 0)]] StructuredBuffer<MeshletRef> gMeshlets;
[[vk::binding(10, 0)]] StructuredBuffer<GpuInstance> gInstanceBuf;
[[vk::binding(12, 0)]] Texture2D<float4> gTex[768];
[[vk::binding(13, 0)]] SamplerState gSamp;
[[vk::binding(14, 0)]] StructuredBuffer<MaterialGpu> gMat;
[[vk::binding(21, 0)]] StructuredBuffer<uint> gCubeVisible;

#ifndef CUBE_FACE
#define CUBE_FACE 0
#endif

struct VertOut {
    float4 pos : SV_Position;
    float3 world : TEXCOORD0;
    nointerpolation uint instanceId : COLOR0;
};

struct PrimOut {
    nointerpolation uint primId : COLOR1;
};

groupshared float4 gClip[64];
groupshared float3 gWorld[64];
groupshared uint gKept;

float3 instancePoint(GpuInstance inst, float3 local) {
    const float4 h = float4(local, 1.0);
    return float3(dot(inst.world0, h), dot(inst.world1, h), dot(inst.world2, h));
}

[shader("mesh")]
[numthreads(128, 1, 1)]
[outputtopology("triangle")]
void cubeMesh(
    uint gtid : SV_GroupThreadID,
    uint gid : SV_GroupID,
    out vertices VertOut verts[64],
    out indices uint3 prims[124],
    out primitives PrimOut primPayload[124])
{
    const uint src = gCubeVisible[gid];
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
        float4 clip = mul(gFrame.cubeFace[CUBE_FACE], float4(world, 1.0));
        const float w = clip.w == 0.0 ? 1.0 : clip.w;
        const float2 local = clip.xy / w * 0.5 + 0.5;
        const float2 atlas = float2((local.x + float(CUBE_FACE)) / 6.0, local.y);
        clip.xy = (atlas * 2.0 - 1.0) * w;
        gClip[gtid] = clip;
        gWorld[gtid] = world;
    }
    GroupMemoryBarrierWithGroupSync();
    uint3 tri = uint3(0, 0, 0);
    bool keep = false;
    if (gtid < triCount) {
        const uint at = meshlet.indexOffset + gtid * 3;
        tri = uint3(gIdx[at], gIdx[at + 1], gIdx[at + 2]);
        const float4 a = gClip[tri.x];
        const float4 b = gClip[tri.y];
        const float4 c = gClip[tri.z];
        keep = tri.x < vertCount && tri.y < vertCount && tri.z < vertCount && (a.w > 1e-4 || b.w > 1e-4 || c.w > 1e-4);
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
        verts[gtid].world = gWorld[gtid];
        verts[gtid].instanceId = src;
    }
    if (keep) {
        prims[slot] = tri;
        primPayload[slot].primId = gtid;
    }
}

[shader("fragment")]
float4 cubeFs(VertOut vin, nointerpolation uint primId : COLOR1) : SV_Target0 {
    const GpuInstance inst = gInstanceBuf[vin.instanceId];
    const MaterialGpu mat = gMat[inst.materialId];
    float3 albedo = mat.baseColor.rgb;
    if (mat.albedo > 0 && mat.albedo < 768) {
        albedo *= gTex[mat.albedo].SampleLevel(gSamp, float2(0.5, 0.5), 4.0).rgb;
    }
    const float3 L = normalize(gFrame.sunDirIntensity.xyz);
    const float ndotl = saturate(dot(normalize(vin.world - gFrame.cubeMin.xyz), L) * 0.5 + 0.5);
    return float4(albedo * (0.2 + 0.8 * ndotl), 1.0);
}

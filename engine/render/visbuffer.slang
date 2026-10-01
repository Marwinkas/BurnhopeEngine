// visbuffer — packed R32 only. Geometry from heap SSBO (BDA-backed).
// Encoding: 10b instance | 14b meshlet | 8b tri

struct FrameGPU {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4 sunDirIntensity;
    float4 sunColor;
    float4 cameraPos;
    float2 invExtent;
    float2 padInv;
    uint2 extent;
    uint2 padExt;
};

struct Vertex {
    float3 pos;
    float pad0;
    float3 nrm;
    float pad1;
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameGPU> gFrame;
[[vk::binding(7, 0)]] StructuredBuffer<Vertex> gVerts;

struct VertOut {
    float4 pos : SV_Position;
    nointerpolation uint visId : COLOR0;
};

uint packVis(uint instanceId, uint meshletId, uint triId) {
    return (instanceId << 22) | (meshletId << 8) | (triId & 255);
}

[shader("mesh")]
[numthreads(32, 1, 1)]
[outputtopology("triangle")]
void visMesh(
    uint gtid : SV_GroupThreadID,
    out vertices VertOut verts[3],
    out indices uint3 prims[1])
{
    if (gtid == 0) {
        SetMeshOutputCounts(3, 1);
    }
    GroupMemoryBarrierWithGroupSync();
    const uint vis = packVis(1, 0, 0);
    if (gtid < 3) {
        const float3 p = gVerts[gtid].pos;
        verts[gtid].pos = mul(gFrame.viewProj, float4(p, 1.0));
        verts[gtid].visId = vis;
    }
    if (gtid == 0) {
        prims[0] = uint3(0, 1, 2);
    }
}

[shader("fragment")]
uint visFs(VertOut vin) : SV_Target0 {
    return vin.visId;
}

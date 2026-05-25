import common.SceneGpu;
import common.SlangBuiltins;
import common.GbufferDebug;
import common.MeshBda;
import common.GfxHeapPC;
import common.SceneAddresses;

[[vk::push_constant]] GfxHeapPC pc;

struct TaskPayload {
    uint objectID;
    uint firstIndex;
    uint lodTriCount;
};

struct MeshVertexOut {
    float4 position : SV_Position;
    float3 outCrntPos : TEXCOORD0;
    float2 outTexCoord : TEXCOORD1;
    float3 outTBN0 : TEXCOORD2;
    float3 outTBN1 : TEXCOORD3;
    float3 outTBN2 : TEXCOORD4;
    nointerpolation uint outMatID : TEXCOORD5;
    float4 outColor : TEXCOORD6;
    float2 outUV2 : TEXCOORD7;
    float outThickness : TEXCOORD8;
};

[shader("mesh")]
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void main(
    in TaskPayload payload,
    out vertices MeshVertexOut vertices[255],
    out indices uint3 triangles[85],
    uint3 gtid : SV_GroupThreadID,
    uint3 gid : SV_GroupID)
{
    uint tid = gtid.x;

    if (meshForceNdcTriangle(pc.gfxDebug)) {
        if (gid.x > 0u) {
            SetMeshOutputCounts(0, 0);
            return;
        }
        SetMeshOutputCounts(3, 1);
        if (tid == 0u) {
            triangles[0] = uint3(0, 1, 2);
        }
        if (tid < 3u) {
            float2 ndc[3] = { float2(-0.9, -0.9), float2(0.9, -0.9), float2(0.0, 0.85) };
            vertices[tid].position = float4(ndc[tid], 0.5, 1.0);
            vertices[tid].outCrntPos = float3(0.0);
            vertices[tid].outMatID = 0u;
            vertices[tid].outTexCoord = float2(0.0);
            vertices[tid].outColor = float4(1.0);
            vertices[tid].outTBN0 = float3(1.0, 0.0, 0.0);
            vertices[tid].outTBN1 = float3(0.0, 1.0, 0.0);
            vertices[tid].outTBN2 = float3(0.0, 0.0, 1.0);
            vertices[tid].outUV2 = float2(0.0);
            vertices[tid].outThickness = 0.0;
        }
        return;
    }

    if (pc.meshPosBda == 0u || pc.meshAttrBda == 0u || pc.meshIdxBda == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint triCount = payload.lodTriCount;
    if (triCount == 0u) {
        triCount = pc.cpuLodTriCount;
    }
    if (triCount == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    uint vertCount = pc.meshVertexCount;
    if (vertCount == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    if (pc.sceneAddressesBda == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }
    SceneAddresses scene = loadSceneAddresses(pc.sceneAddressesBda);
    if (scene.objectStorageBda == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    row_major float4x4 modelMatrix;
    meshLoadModelMatrix(scene.objectStorageBda, payload.objectID, modelMatrix);
    float3 aabbMin;
    float3 aabbMax;
    meshLoadObjectAabb(scene.objectStorageBda, payload.objectID, aabbMin, aabbMax);
    float3 extent = max(aabbMax - aabbMin, float3(1e-6, 1e-6, 1e-6));

    uint startTriangle = gid.x * 85u;
    uint localTriCount = min(85u, triCount - startTriangle);
    if (localTriCount == 0u) {
        SetMeshOutputCounts(0, 0);
        return;
    }

    SetMeshOutputCounts(localTriCount * 3u, localTriCount);

    if (tid < localTriCount) {
        triangles[tid] = uint3(tid * 3u, tid * 3u + 1u, tid * 3u + 2u);
    }

    // Same layout as the autonomous test that drew the cube: one tri per tid, vIdx = tid*3+k.
    if (tid < localTriCount) {
        uint globalTriBase = (startTriangle + tid) * 3u;

        for (uint k = 0u; k < 3u; k++) {
            uint vIdx = tid * 3u + k;
            uint corner = globalTriBase + k;
            uint globalVtxIdx = min(meshLoadIndex(pc.meshIdxBda, corner), vertCount - 1u);

            PackedVertexPos vtxP = meshLoadPos(pc.meshPosBda, globalVtxIdx);
            PackedVertexAttr vtxA = meshLoadAttr(pc.meshAttrBda, globalVtxIdx);

            float3 localPosNorm = float3(
                float(vtxP.x) / 65535.0,
                float(vtxP.y) / 65535.0,
                float(vtxP.z) / 65535.0
            );
            float3 localPos = aabbMin + localPosNorm * extent;

            float4 clipPos = mul(mul(mul(float4(localPos, 1.0), modelMatrix), pc.meshView), pc.meshProjection);
            vertices[vIdx].position = clipPos;
            vertices[vIdx].outCrntPos = mul(float4(localPos, 1.0), modelMatrix).xyz;
            vertices[vIdx].outMatID =
                meshLoadMaterialId(scene.objectStorageBda, payload.objectID);
            vertices[vIdx].outTexCoord = unpackHalf2x16(vtxA.texUV);
            vertices[vIdx].outColor = float4(1.0);
            vertices[vIdx].outTBN0 = float3(0.0, 0.0, 1.0);
            vertices[vIdx].outTBN1 = float3(0.0, 1.0, 0.0);
            vertices[vIdx].outTBN2 = float3(1.0, 0.0, 0.0);
            vertices[vIdx].outUV2 = float2(0.0);
            vertices[vIdx].outThickness = float((vtxP.pad >> 8) & 0xFF) / 255.0;
        }
    }
}

import common.SceneGpu;
import common.GbufferDebug;
import common.GfxHeapPC;

[[vk::push_constant]] GfxHeapPC pc;

struct TaskPayload {
    uint objectID;
    uint firstIndex;
    uint lodTriCount;
};

groupshared TaskPayload payload;
groupshared uint sharedMeshGroups;

[shader("task")]
[numthreads(32, 1, 1)]
void main(uint3 gtid : SV_GroupThreadID, uint gid : SV_GroupID) {
    uint objIdx = gid.x;

    if (gtid.x == 0) {
        payload.objectID = objIdx;
        payload.firstIndex = 0u;
        if (meshForceNdcTriangle(pc.gfxDebug)) {
            payload.lodTriCount = 1u;
            sharedMeshGroups = 1u;
        } else {
            uint triCount = pc.cpuLodTriCount;
            if (triCount == 0u && pc.meshVertexCount > 0u && pc.meshIdxBda != 0u) {
                triCount = max(1u, pc.meshVertexCount / 3u);
            }
            payload.lodTriCount = triCount;
            sharedMeshGroups = triCount > 0u ? (triCount + 84u) / 85u : 0u;
        }
    }

    GroupMemoryBarrierWithGroupSync();
    DispatchMesh(sharedMeshGroups, 1, 1, payload);
}

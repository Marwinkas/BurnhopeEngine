#include "rhi/MeshDraw.hpp"

namespace burnhope {

void meshDrawRecord(VkCommandBuffer cmd, const MeshDrawDesc& desc) {
    if (desc.groupCount > 0 && desc.mesh != VK_NULL_HANDLE) {
        cmdBindMeshFrag(cmd, desc.mesh, desc.frag);
        vkCmdDrawMeshTasksEXT(cmd, desc.groupCount, 1, 1);
        return;
    }
    if (desc.vertexCount > 0 && desc.vert != VK_NULL_HANDLE) {
        cmdBindVertFrag(cmd, desc.vert, desc.frag, desc.vertNullMeshStage);
        vkCmdDraw(cmd, desc.vertexCount, 1, 0, 0);
    }
}

} // namespace burnhope

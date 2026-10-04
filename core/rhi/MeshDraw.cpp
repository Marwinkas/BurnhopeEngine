#include "rhi/MeshDraw.hpp"

namespace burnhope {

void meshDrawRecord(VkCommandBuffer cmd, const MeshDrawDesc& desc) {
    if (desc.pushData != nullptr && desc.pushBytes > 0 && desc.pushLayout != VK_NULL_HANDLE) {
        vkCmdPushConstants(cmd, desc.pushLayout, desc.pushStages, 0, desc.pushBytes, desc.pushData);
    }
    if (desc.mesh != VK_NULL_HANDLE && (desc.indirect != VK_NULL_HANDLE || desc.groupCount > 0)) {
        cmdBindMeshFrag(cmd, desc.mesh, desc.frag);
        if (desc.indirect != VK_NULL_HANDLE) {
            vkCmdDrawMeshTasksIndirectEXT(cmd, desc.indirect, desc.indirectOffset, 1, 16);
        } else {
            vkCmdDrawMeshTasksEXT(cmd, desc.groupCount, 1, 1);
        }
        return;
    }
    if (desc.vertexCount > 0 && desc.vert != VK_NULL_HANDLE) {
        cmdBindVertFrag(cmd, desc.vert, desc.frag, desc.vertNullMeshStage);
        vkCmdDraw(cmd, desc.vertexCount, 1, 0, 0);
    }
}

} // namespace burnhope

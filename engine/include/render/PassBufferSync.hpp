#pragma once

#include "rhi/Barrier.hpp"

namespace burnhope {

inline void bufferBarrierComputeWriteToRead(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size) {
    rhiBufferBarrier(cmd, buffer, offset, size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

inline void bufferBarrierComputeToIndirect(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
    rhiBufferBarrier(cmd, buffer, 0, size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

inline void bufferBarrierComputeToMeshShader(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
    rhiBufferBarrier(cmd, buffer, 0, size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

// Запись compute в чтение и запись следующего compute. Середина двухфазного кулинга.
inline void bufferBarrierComputeWriteToReadWrite(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size) {
    rhiBufferBarrier(cmd, buffer, 0, size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

inline void bufferBarrierHostToAccel(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize size = VK_WHOLE_SIZE) {
    rhiBufferBarrier(cmd, buffer, 0, size,
        VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
}

inline void bufferBarrierToTransfer(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_HOST_BIT,
    VkAccessFlags2 srcAccess = VK_ACCESS_2_HOST_WRITE_BIT) {
    rhiBufferBarrier(cmd, buffer, offset, size, srcStage, srcAccess, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
}

inline void bufferBarrierTransferToCompute(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkAccessFlags2 dstAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    rhiBufferBarrier(cmd, buffer, offset, size,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, dstAccess);
}

// Хост записал кадр. По умолчанию его читают compute и mesh как uniform.
inline void bufferBarrierHostToShader(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize size,
    VkPipelineStageFlags2 dstStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
    VkAccessFlags2 dstAccess = VK_ACCESS_2_UNIFORM_READ_BIT) {
    rhiBufferBarrier(cmd, buffer, 0, size, VK_PIPELINE_STAGE_2_HOST_BIT, VK_ACCESS_2_HOST_WRITE_BIT, dstStage, dstAccess);
}

// Хост или прошлый indirect/mesh готовят буфер к compute. Маски задаёт вызывающий.
inline void bufferBarrierToCompute(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkAccessFlags2 dstAccess = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT) {
    rhiBufferBarrier(cmd, buffer, offset, size, srcStage, srcAccess, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, dstAccess);
}

} // namespace burnhope

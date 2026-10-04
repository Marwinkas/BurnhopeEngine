#pragma once

#include "rhi/GpuImage.hpp"

#include <volk.h>

namespace burnhope {

inline void rhiImageBarrier(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldL,
    VkImageLayout newL,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT) {
    if (image == VK_NULL_HANDLE) {
        return;
    }
    VkImageMemoryBarrier2 b{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .oldLayout = oldL,
        .newLayout = newL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &b,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

inline void rhiBufferBarrier(
    VkCommandBuffer cmd,
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize size,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess) {
    VkBufferMemoryBarrier2 b{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = buffer,
        .offset = offset,
        .size = size,
    };
    VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 1,
        .pBufferMemoryBarriers = &b,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

inline void rhiMemoryBarrier(
    VkCommandBuffer cmd,
    VkPipelineStageFlags2 srcStage,
    VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage,
    VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 b{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess,
    };
    VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &b,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
}

// Если раскладка, стадия и доступ уже такие — барьер не ставится.
// depend ставит зависимость даже в том же состоянии: два compute подряд пишут одну картинку.
inline void imageBarrier(
    VkCommandBuffer cmd,
    TrackedImage& img,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 newStage,
    VkAccessFlags2 newAccess,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    bool depend = false) {
    if (img.image.image == VK_NULL_HANDLE) {
        return;
    }
    if (!depend && img.layout != VK_IMAGE_LAYOUT_UNDEFINED && img.layout == newLayout && img.stage == newStage && img.access == newAccess) {
        return;
    }
    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
    if (img.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
        srcStage = img.stage;
        srcAccess = img.access;
    }
    rhiImageBarrier(cmd, img.image.image, img.layout, newLayout, srcStage, srcAccess, newStage, newAccess, aspect);
    img.layout = newLayout;
    img.stage = newStage;
    img.access = newAccess;
}

} // namespace burnhope

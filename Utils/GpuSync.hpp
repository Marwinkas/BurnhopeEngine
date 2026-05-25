#pragma once

#include "Device.hpp"
#include <vulkan/vulkan.h>

namespace burnhope {

/** Split barrier: set event on CB, wait on same or other CB (no monolithic pipelineBarrier2 in loops). */
struct ImageBarrierEvent final {
  VkEvent event{VK_NULL_HANDLE};
  VkImageMemoryBarrier2 barrier{};
};

inline void cmdSetImageBarrierEvent(
    VkCommandBuffer cmd,
    VkEvent event,
    const VkImageMemoryBarrier2& barrier) {
  VkDependencyInfo dep{};
  dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &barrier;
  vkCmdSetEvent2(cmd, event, &dep);
}

inline void cmdWaitImageBarrierEvent(
    VkCommandBuffer cmd,
    VkEvent event,
    const VkImageMemoryBarrier2& barrier) {
  VkDependencyInfo dep{};
  dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &barrier;
  vkCmdWaitEvents2(cmd, 1, &event, &dep);
}

/** One-shot upload barrier helper (init path only). */
inline VkImageMemoryBarrier2 makeImageBarrier2(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkAccessFlags2 srcAccess,
    VkAccessFlags2 dstAccess,
    VkPipelineStageFlags2 srcStage,
    VkPipelineStageFlags2 dstStage,
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT,
    uint32_t mipLevels = 1,
    uint32_t layers = 1) {
  VkImageMemoryBarrier2 b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  b.oldLayout = oldLayout;
  b.newLayout = newLayout;
  b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  b.image = image;
  b.subresourceRange = {aspect, 0, mipLevels, 0, layers};
  b.srcAccessMask = srcAccess;
  b.dstAccessMask = dstAccess;
  b.srcStageMask = srcStage;
  b.dstStageMask = dstStage;
  return b;
}

} // namespace burnhope

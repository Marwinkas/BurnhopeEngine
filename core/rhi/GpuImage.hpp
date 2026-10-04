#pragma once

#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t mips = 1;
};

// Картинка и последнее состояние барьера. Ядро не знает, чей это проход.
struct TrackedImage {
    GpuImage image{};
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

[[nodiscard]] bool gpuImageCreate(
    GpuImage& img,
    Device& d,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    uint32_t mipLevels = 1);

void gpuImageDestroy(GpuImage& img, Device& d);

} // namespace burnhope

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

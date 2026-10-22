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
    uint32_t depth = 1;
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

// Плотный 3D-объём со всей цепочкой мипов. Сторона — степень двойки. Вид один, на все мипы.
[[nodiscard]] bool gpuImageCreateFroxel(
    GpuImage& img,
    Device& d,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect);

[[nodiscard]] bool gpuImageCreateVolume(
    GpuImage& img,
    Device& d,
    uint32_t side,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect);

void gpuImageDestroy(GpuImage& img, Device& d);

} // namespace burnhope

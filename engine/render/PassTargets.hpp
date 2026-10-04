#pragma once

#include "rhi/GpuImage.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>

namespace burnhope {

inline void destroyTracked(Device& device, std::initializer_list<TrackedImage*> pics) {
    for (TrackedImage* pic : pics) {
        if (pic != nullptr) {
            gpuImageDestroy(pic->image, device);
            pic->layout = VK_IMAGE_LAYOUT_UNDEFINED;
            pic->stage = VK_PIPELINE_STAGE_2_NONE;
            pic->access = VK_ACCESS_2_NONE;
        }
    }
}

// Половина кадра. Ноль по любой стороне становится один тексель.
[[nodiscard]] inline VkExtent2D halfExtent(VkExtent2D extent) {
    return {std::max(extent.width / 2u, 1u), std::max(extent.height / 2u, 1u)};
}

// Снимает старую картинку и создаёт новую. Сторона меньше одного текселя поднимается до одного.
[[nodiscard]] inline bool recreateImage(
    GpuImage& img,
    Device& device,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect) {
    gpuImageDestroy(img, device);
    if (extent.width == 0u) {
        extent.width = 1u;
    }
    if (extent.height == 0u) {
        extent.height = 1u;
    }
    return gpuImageCreate(img, device, extent, format, usage, aspect);
}

[[nodiscard]] inline bool recreateTracked(
    TrackedImage& img,
    Device& device,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect) {
    const bool ok = recreateImage(img.image, device, extent, format, usage, aspect);
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    img.stage = VK_PIPELINE_STAGE_2_NONE;
    img.access = VK_ACCESS_2_NONE;
    return ok;
}

[[nodiscard]] inline bool recreateHdr(GpuImage& img, Device& device, VkExtent2D extent) {
    return recreateImage(
        img, device, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
}

} // namespace burnhope

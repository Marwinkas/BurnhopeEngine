#include "rhi/GpuImage.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool gpuImageCreate(
    GpuImage& img,
    Device& d,
    VkExtent2D extent,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspect,
    uint32_t mipLevels) {
    gpuImageDestroy(img, d);
    if (mipLevels < 1) {
        mipLevels = 1;
    }
    img.format = format;
    img.extent = extent;
    img.mips = mipLevels;

    VkImageCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {extent.width, extent.height, 1},
        .mipLevels = mipLevels,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    if (vmaCreateImage(d.allocator, &ci, &aci, &img.image, &img.alloc, nullptr) != VK_SUCCESS) {
        spdlog::error("vmaCreateImage failed");
        return false;
    }

    VkImageViewCreateInfo vi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = {aspect, 0, mipLevels, 0, 1},
    };
    if (vkCreateImageView(d.device, &vi, nullptr, &img.view) != VK_SUCCESS) {
        spdlog::error("vkCreateImageView failed");
        gpuImageDestroy(img, d);
        return false;
    }
    return true;
}

void gpuImageDestroy(GpuImage& img, Device& d) {
    if (d.device == VK_NULL_HANDLE) {
        img = {};
        return;
    }
    if (img.view != VK_NULL_HANDLE) {
        vkDestroyImageView(d.device, img.view, nullptr);
    }
    if (img.image != VK_NULL_HANDLE) {
        vmaDestroyImage(d.allocator, img.image, img.alloc);
    }
    img = {};
}

} // namespace burnhope

#include "ui/Font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace burnhope {
namespace {

constexpr int kCell = 64;
constexpr int kCols = 16;
constexpr int kRows = 12;

uint32_t codepointAt(int index) {
    if (index < 95) {
        return static_cast<uint32_t>(32 + index);
    }
    if (index == 95) {
        return 0x0401;
    }
    if (index < 160) {
        return 0x0410u + static_cast<uint32_t>(index - 96);
    }
    if (index == 160) {
        return 0x0451;
    }
    return 0;
}

bool uploadAtlas(Device& device, DescriptorHeaps& heaps, GpuImage& atlas, const std::vector<uint8_t>& pixels, int width, int height) {
    gpuImageDestroy(atlas, device);
    if (!gpuImageCreate(
            atlas,
            device,
            VkExtent2D{static_cast<uint32_t>(width), static_cast<uint32_t>(height)},
            VK_FORMAT_R8_UNORM,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }

    GpuBuffer staging{};
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width * height);
    if (!gpuBufferCreate(staging, device, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) {
        return false;
    }
    std::memcpy(staging.mapped, pixels.data(), static_cast<size_t>(bytes));
    gpuBufferFlush(staging, device, 0, bytes);

    // Общий пул/очередь device.commandPool — см. комментарий у Device::queueMutex (Device.hpp).
    std::lock_guard<std::mutex> uploadLock(device.queueMutex);
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = device.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(device.device, &alloc, &cmd) != VK_SUCCESS) {
        gpuBufferDestroy(staging, device);
        return false;
    }
    VkCommandBufferBeginInfo begin{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cmd, &begin);

    VkImageMemoryBarrier2 toCopy{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = atlas.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toCopy,
    };
    vkCmdPipelineBarrier2(cmd, &dep);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyBufferToImage(cmd, staging.buffer, atlas.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier2 toSample{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = atlas.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
    };
    dep.pImageMemoryBarriers = &toSample;
    vkCmdPipelineBarrier2(cmd, &dep);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cbsi{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = cmd,
    };
    VkSubmitInfo2 submit{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &cbsi,
    };
    const bool ok = vkQueueSubmit2(device.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    if (ok) {
        vkQueueWaitIdle(device.graphicsQueue);
    }
    vkFreeCommandBuffers(device.device, device.commandPool, 1, &cmd);
    gpuBufferDestroy(staging, device);
    if (!ok) {
        return false;
    }

    if (!heapWriteImage(
            heaps,
            device,
            0,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            atlas,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 16.0f,
    };
    return heapWriteSampler(heaps, device, 0, sampler);
}

} // namespace

bool fontLoadSdf(
    UiState& ui,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    const char* path,
    float pixelSize) {
    if (path == nullptr || pixelSize < 4.0f) {
        return false;
    }
    FT_Library library{};
    if (FT_Init_FreeType(&library) != 0) {
        spdlog::error("freetype init failed");
        return false;
    }
    FT_UInt spread = 2;
    FT_Property_Set(library, "sdf", "spread", &spread);
    FT_Face face{};
    if (FT_New_Face(library, path, 0, &face) != 0) {
        spdlog::error("font open failed: {}", path);
        FT_Done_FreeType(library);
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));

    const int width = kCols * kCell;
    const int height = kRows * kCell;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height), 0);

    for (int i = 0; i < static_cast<int>(kUiFontGlyphs); ++i) {
        const uint32_t cp = codepointAt(i);
        ui.glyphAdv[i] = pixelSize * 0.5f;
        ui.glyphW[i] = 0;
        ui.glyphH[i] = 0;
        if (cp == 0 || FT_Load_Char(face, cp, FT_LOAD_DEFAULT) != 0) {
            continue;
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0) {
            continue;
        }
        const FT_Bitmap& bmp = face->glyph->bitmap;
        const int col = i % kCols;
        const int row = i / kCols;
        const int ox = col * kCell + (kCell - static_cast<int>(bmp.width)) / 2;
        const int oy = row * kCell + (kCell - static_cast<int>(bmp.rows)) / 2;
        for (unsigned y = 0; y < bmp.rows; ++y) {
            const int dy = oy + static_cast<int>(y);
            if (dy < 0 || dy >= height) {
                continue;
            }
            for (unsigned x = 0; x < bmp.width; ++x) {
                const int dx = ox + static_cast<int>(x);
                if (dx < 0 || dx >= width) {
                    continue;
                }
                const unsigned char* src = bmp.buffer + static_cast<int>(y) * bmp.pitch;
            pixels[static_cast<size_t>(dy * width + dx)] = src[x];
            }
        }
        ui.glyphU0[i] = static_cast<float>(ox) / static_cast<float>(width);
        ui.glyphV0[i] = static_cast<float>(oy) / static_cast<float>(height);
        ui.glyphU1[i] = static_cast<float>(ox + static_cast<int>(bmp.width)) / static_cast<float>(width);
        ui.glyphV1[i] = static_cast<float>(oy + static_cast<int>(bmp.rows)) / static_cast<float>(height);
        ui.glyphBx[i] = static_cast<float>(face->glyph->bitmap_left);
        ui.glyphBy[i] = static_cast<float>(face->glyph->bitmap_top);
        ui.glyphW[i] = static_cast<float>(bmp.width);
        ui.glyphH[i] = static_cast<float>(bmp.rows);
        ui.glyphAdv[i] = static_cast<float>(face->glyph->advance.x) / 64.0f;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);

    if (!uploadAtlas(device, heaps, atlas, pixels, width, height)) {
        spdlog::error("sdf atlas upload failed");
        return false;
    }
    ui.fontPx = pixelSize;
    ui.fontReady = true;
    ui.look.advance = ui.glyphAdv[static_cast<int>('M' - 32)] * (ui.look.glyphH / pixelSize);
    if (ui.look.advance < 4.0f) {
        ui.look.advance = 9.0f;
    }
    spdlog::info("sdf font {} at {} px", path, pixelSize);
    return true;
}

} // namespace burnhope

#include "image/ImageUpload.hpp"

#include "image/ImageCompress.hpp"
#include "io/File.hpp"
#include "platform/Check.hpp"
#include "platform/Jobs.hpp"
#include "rhi/GpuBuffer.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace burnhope {
namespace {

bool formatOk(const Device& d, VkFormat format) {
    VkFormatProperties props{};
    vkGetPhysicalDeviceFormatProperties(d.physical, format, &props);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT
        | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (props.optimalTilingFeatures & need) == need;
}

const char* formatName(VkFormat format) {
    switch (format) {
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        return "BC1";
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        return "BC1s";
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return "BC3";
    case VK_FORMAT_BC5_UNORM_BLOCK:
        return "BC5";
    case VK_FORMAT_BC7_UNORM_BLOCK:
        return "BC7";
    case VK_FORMAT_BC7_SRGB_BLOCK:
        return "BC7s";
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        return "BC6H";
    case VK_FORMAT_R8G8B8A8_SRGB:
        return "RGBA8s";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "RGBA16F";
    default:
        return "RGBA8";
    }
}

uint32_t align4(uint32_t v) {
    return (v + 3u) & ~3u;
}

void downsampleRgba(const std::vector<uint8_t>& src, uint32_t sw, uint32_t sh, std::vector<uint8_t>& dst, uint32_t& dw, uint32_t& dh) {
    dw = sw > 1 ? sw / 2u : 1u;
    dh = sh > 1 ? sh / 2u : 1u;
    dst.resize(static_cast<size_t>(dw) * dh * 4u);
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            uint16_t acc[4]{};
            for (uint32_t oy = 0; oy < 2; ++oy) {
                for (uint32_t ox = 0; ox < 2; ++ox) {
                    const uint32_t sx = x * 2u + ox < sw ? x * 2u + ox : sw - 1u;
                    const uint32_t sy = y * 2u + oy < sh ? y * 2u + oy : sh - 1u;
                    const uint8_t* p = src.data() + (static_cast<size_t>(sy) * sw + sx) * 4u;
                    acc[0] = static_cast<uint16_t>(acc[0] + p[0]);
                    acc[1] = static_cast<uint16_t>(acc[1] + p[1]);
                    acc[2] = static_cast<uint16_t>(acc[2] + p[2]);
                    acc[3] = static_cast<uint16_t>(acc[3] + p[3]);
                }
            }
            uint8_t* o = dst.data() + (static_cast<size_t>(y) * dw + x) * 4u;
            o[0] = static_cast<uint8_t>(acc[0] / 4u);
            o[1] = static_cast<uint8_t>(acc[1] / 4u);
            o[2] = static_cast<uint8_t>(acc[2] / 4u);
            o[3] = static_cast<uint8_t>(acc[3] / 4u);
        }
    }
}

void downsampleRgb(const std::vector<float>& src, uint32_t sw, uint32_t sh, std::vector<float>& dst, uint32_t& dw, uint32_t& dh) {
    dw = sw > 1 ? sw / 2u : 1u;
    dh = sh > 1 ? sh / 2u : 1u;
    dst.resize(static_cast<size_t>(dw) * dh * 3u);
    for (uint32_t y = 0; y < dh; ++y) {
        for (uint32_t x = 0; x < dw; ++x) {
            float acc[3]{};
            for (uint32_t oy = 0; oy < 2; ++oy) {
                for (uint32_t ox = 0; ox < 2; ++ox) {
                    const uint32_t sx = x * 2u + ox < sw ? x * 2u + ox : sw - 1u;
                    const uint32_t sy = y * 2u + oy < sh ? y * 2u + oy : sh - 1u;
                    const float* p = src.data() + (static_cast<size_t>(sy) * sw + sx) * 3u;
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                }
            }
            float* o = dst.data() + (static_cast<size_t>(y) * dw + x) * 3u;
            o[0] = acc[0] * 0.25f;
            o[1] = acc[1] * 0.25f;
            o[2] = acc[2] * 0.25f;
        }
    }
}

bool hostCopyOk(const Device& d, VkFormat format) {
    if (!d.caps.hostImageCopy) {
        return false;
    }
    VkFormatProperties3 props3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 props2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &props3};
    vkGetPhysicalDeviceFormatProperties2(d.physical, format, &props2);
    return (props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) != 0;
}

bool submitImageHostCopy(GpuImage& img, Device& d, VkFormat format, const uint8_t* data, const ImageMip* levels, uint32_t mipCount) {
    BH_ASSERT(mipCount <= 16, "submitImageHostCopy: mipCount exceeds fixed region array");
    if (!gpuImageCreate(
            img,
            d,
            VkExtent2D{levels[0].width, levels[0].height},
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_HOST_TRANSFER_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipCount)) {
        return false;
    }
    const VkImageSubresourceRange fullRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1};
    VkHostImageLayoutTransitionInfo toDst{
        .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
        .image = img.image,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .subresourceRange = fullRange,
    };
    if (vkTransitionImageLayout(d.device, 1, &toDst) != VK_SUCCESS) {
        gpuImageDestroy(img, d);
        return false;
    }
    VkMemoryToImageCopy regions[16]{};
    for (uint32_t i = 0; i < mipCount; ++i) {
        regions[i] = VkMemoryToImageCopy{
            .sType = VK_STRUCTURE_TYPE_MEMORY_TO_IMAGE_COPY,
            .pHostPointer = data + levels[i].offset,
            .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1},
            .imageExtent = {levels[i].width, levels[i].height, 1},
        };
    }
    VkCopyMemoryToImageInfo copyInfo{
        .sType = VK_STRUCTURE_TYPE_COPY_MEMORY_TO_IMAGE_INFO,
        .dstImage = img.image,
        .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .regionCount = mipCount,
        .pRegions = regions,
    };
    if (vkCopyMemoryToImage(d.device, &copyInfo) != VK_SUCCESS) {
        gpuImageDestroy(img, d);
        return false;
    }
    VkHostImageLayoutTransitionInfo toShader{
        .sType = VK_STRUCTURE_TYPE_HOST_IMAGE_LAYOUT_TRANSITION_INFO,
        .image = img.image,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .subresourceRange = fullRange,
    };
    if (vkTransitionImageLayout(d.device, 1, &toShader) != VK_SUCCESS) {
        gpuImageDestroy(img, d);
        return false;
    }
    spdlog::info("image {} {}x{} mips {} host-copy", formatName(format), levels[0].width, levels[0].height, mipCount);
    return true;
}

bool submitImage(GpuImage& img, Device& d, VkFormat format, const uint8_t* data, VkDeviceSize bytes, const ImageMip* levels, uint32_t mipCount) {
    BH_ASSERT(mipCount <= 16, "submitImage: mipCount exceeds fixed region array");
    if (mipCount <= 16 && hostCopyOk(d, format) && submitImageHostCopy(img, d, format, data, levels, mipCount)) {
        return true;
    }
    if (!gpuImageCreate(
            img,
            d,
            VkExtent2D{levels[0].width, levels[0].height},
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipCount)) {
        return false;
    }
    GpuBuffer staging{};
    if (!gpuBufferCreate(staging, d, bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true)) {
        gpuImageDestroy(img, d);
        return false;
    }
    std::memcpy(staging.mapped, data, static_cast<size_t>(bytes));
    gpuBufferFlush(staging, d, 0, bytes);

    std::lock_guard<std::mutex> uploadLock(d.queueMutex);
    const bool split = d.transferPool != VK_NULL_HANDLE && d.transferPool != d.commandPool;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = split ? d.transferPool : d.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(d.device, &alloc, &cmd) != VK_SUCCESS) {
        gpuBufferDestroy(staging, d);
        gpuImageDestroy(img, d);
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
        .image = img.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1},
    };
    VkDependencyInfo dep{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toCopy,
    };
    vkCmdPipelineBarrier2(cmd, &dep);
    VkBufferImageCopy regions[16]{};
    for (uint32_t i = 0; i < mipCount; ++i) {
        regions[i].bufferOffset = levels[i].offset;
        regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
        regions[i].imageExtent = {levels[i].width, levels[i].height, 1};
    }
    vkCmdCopyBufferToImage(cmd, staging.buffer, img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipCount, regions);
    VkImageMemoryBarrier2 toSample{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = split ? d.caps.transferFamily : VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = split ? d.caps.graphicsFamily : VK_QUEUE_FAMILY_IGNORED,
        .image = img.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1},
    };
    if (split) {
        toSample.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toSample.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
        toSample.dstAccessMask = VK_ACCESS_2_NONE;
    }
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
    VkQueue queue = split ? d.transferQueue : d.graphicsQueue;
    const bool okCopy = vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    if (okCopy) {
        vkQueueWaitIdle(queue);
    }
    vkFreeCommandBuffers(d.device, alloc.commandPool, 1, &cmd);
    bool ok = okCopy;
    if (ok && split) {
        VkCommandBuffer acquire = VK_NULL_HANDLE;
        alloc.commandPool = d.commandPool;
        if (vkAllocateCommandBuffers(d.device, &alloc, &acquire) == VK_SUCCESS) {
            vkBeginCommandBuffer(acquire, &begin);
            toSample.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            toSample.srcAccessMask = VK_ACCESS_2_NONE;
            toSample.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            toSample.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            toSample.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toSample.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            dep.pImageMemoryBarriers = &toSample;
            vkCmdPipelineBarrier2(acquire, &dep);
            vkEndCommandBuffer(acquire);
            cbsi.commandBuffer = acquire;
            ok = vkQueueSubmit2(d.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
            if (ok) {
                vkQueueWaitIdle(d.graphicsQueue);
            }
            vkFreeCommandBuffers(d.device, d.commandPool, 1, &acquire);
        } else {
            ok = false;
        }
    }
    gpuBufferDestroy(staging, d);
    if (!ok) {
        gpuImageDestroy(img, d);
        spdlog::error("image upload failed");
        return false;
    }
    spdlog::info(
        "image {} {}x{} mips {}{}",
        formatName(format),
        levels[0].width,
        levels[0].height,
        mipCount,
        format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R16G16B16A16_SFLOAT ? "" : " ispc");
    return true;
}

uint32_t mipCountFor(uint32_t width, uint32_t height, bool chain) {
    if (!chain || width < 8 || height < 8) {
        return 1;
    }
    uint32_t count = 1;
    while (width >= 8 && height >= 8 && count < 12) {
        width /= 2u;
        height /= 2u;
        if ((width % 4u) != 0 || (height % 4u) != 0) {
            break;
        }
        ++count;
    }
    return count;
}

bool hasAlpha(const uint8_t* rgba, uint32_t pixels) {
    for (uint32_t i = 0; i < pixels; ++i) {
        if (rgba[i * 4u + 3u] < 250) {
            return true;
        }
    }
    return false;
}

VkFormat vkBlock(ImageBlock block, bool srgb) {
    switch (block) {
    case ImageBlock::Bc1:
        return srgb ? VK_FORMAT_BC1_RGBA_SRGB_BLOCK : VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case ImageBlock::Bc3:
        return srgb ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    case ImageBlock::Bc5:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case ImageBlock::Bc7:
        return srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    case ImageBlock::Bc4:
        return VK_FORMAT_BC4_UNORM_BLOCK;
    case ImageBlock::Bc6h:
        return VK_FORMAT_BC6H_UFLOAT_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

struct MipCpu {
    const uint8_t* src = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint8_t* dst = nullptr;
};

void mipCpuJob(void* user) {
    const auto* job = static_cast<const MipCpu*>(user);
    compressRgbaLevel(job->src, job->width, job->height, job->format, job->dst);
}

bool uploadBlockChain(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint32_t frames,
    VkFormat format,
    ImageBlock block,
    const char* cacheDds) {
    const uint32_t frameH = height / frames;
    const uint32_t pw = align4(width);
    const uint32_t ph = align4(frameH);
    const uint32_t stacked = ph * frames;
    std::vector<uint8_t> padded(static_cast<size_t>(pw) * stacked * 4u);
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t y = 0; y < ph; ++y) {
            const uint32_t sy = y < frameH ? y : frameH - 1u;
            for (uint32_t x = 0; x < pw; ++x) {
                const uint32_t sx = x < width ? x : width - 1u;
                const uint8_t* src = rgba + (static_cast<size_t>(frame * frameH + sy) * width + sx) * 4u;
                uint8_t* dst = padded.data() + (static_cast<size_t>(frame * ph + y) * pw + x) * 4u;
                std::memcpy(dst, src, 4);
            }
        }
    }
    const bool chain = frames == 1;
    const uint32_t mips = mipCountFor(pw, stacked, chain);
    const uint32_t blockBytes = imageBlockBytes(block);
    std::vector<std::vector<uint8_t>> chainPx;
    chainPx.push_back(std::move(padded));
    uint32_t lw = pw;
    uint32_t lh = stacked;
    while (chainPx.size() < mips) {
        std::vector<uint8_t> next;
        downsampleRgba(chainPx.back(), lw, lh, next, lw, lh);
        chainPx.push_back(std::move(next));
    }
    ImageMip levels[16]{};
    std::vector<uint8_t> packed;
    lw = pw;
    lh = stacked;
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint64_t bytes = static_cast<uint64_t>(lw / 4u) * (lh / 4u) * blockBytes;
        levels[mip].width = lw;
        levels[mip].height = lh;
        levels[mip].offset = packed.size();
        levels[mip].bytes = bytes;
        packed.resize(packed.size() + static_cast<size_t>(bytes));
        lw = lw > 1 ? lw / 2u : 1u;
        lh = lh > 1 ? lh / 2u : 1u;
    }
    const bool gpu0 = levels[0].width >= 2048u || levels[0].height >= 2048u;
    bool gpuKicked = false;
    if (gpu0) {
        gpuKicked = gpuCompressLevelDeferred(d, block, chainPx[0].data(), levels[0].width, levels[0].height, packed.data());
        if (!gpuKicked) {
            compressRgbaLevel(chainPx[0].data(), levels[0].width, levels[0].height, format, packed.data());
        }
    }
    MipCpu cpu[16]{};
    Job jobs[16]{};
    int jobCount = 0;
    for (uint32_t mip = gpuKicked ? 1u : 0u; mip < mips; ++mip) {
        cpu[jobCount] = MipCpu{
            chainPx[mip].data(),
            levels[mip].width,
            levels[mip].height,
            format,
            packed.data() + levels[mip].offset,
        };
        jobs[jobCount] = Job{mipCpuJob, &cpu[jobCount]};
        jobCount += 1;
    }
    if (jobCount > 0) {
        jobsRunAndWait(deviceJobs(d), jobs, jobCount);
    }
    if (gpuKicked) {
        gpuCompressJoin(d);
    }
    writeBlockDds(cacheDds, levels[0].width, levels[0].height, mips, block, packed.data(), packed.size());
    return submitImage(img, d, format, packed.data(), packed.size(), levels, mips);
}

} // namespace

bool imageUploadCompressed(GpuImage& img, Device& d, VkFormat format, const uint8_t* blocks, uint32_t width, uint32_t height) {
    if (blocks == nullptr || width == 0 || height == 0) {
        return false;
    }
    ImageMip level{};
    level.width = width;
    level.height = height;
    const uint32_t bx = (width + 3u) / 4u;
    const uint32_t by = (height + 3u) / 4u;
    const uint32_t blockBytes = format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? 8u : 16u;
    level.bytes = static_cast<uint64_t>(bx) * by * blockBytes;
    return submitImageHostCopy(img, d, format, blocks, &level, 1);
}

bool imageLoadDds(GpuImage& img, Device& d, const char* path, bool srgb) {
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped) || mapped.size < 128) {
        return false;
    }
    DdsView view{};
    if (!imageParseDds(mapped.data, mapped.size, view)) {
        fileUnmap(&mapped);
        return false;
    }
    const VkFormat format = vkBlock(view.block, srgb);
    const bool ok = format != VK_FORMAT_UNDEFINED && submitImageHostCopy(img, d, format, view.base, view.levels, view.levelCount);
    fileUnmap(&mapped);
    return ok;
}

bool gpuImageUploadRgba(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint32_t frames,
    const char* cacheDds) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    if (frames == 0) {
        frames = 1;
    }
    const bool alpha = hasAlpha(rgba, width * height);
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    ImageBlock block = ImageBlock::Bc1;
    if (!alpha && formatOk(d, VK_FORMAT_BC1_RGBA_UNORM_BLOCK)) {
        format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        block = ImageBlock::Bc1;
    } else if (alpha && formatOk(d, VK_FORMAT_BC7_UNORM_BLOCK)) {
        format = VK_FORMAT_BC7_UNORM_BLOCK;
        block = ImageBlock::Bc7;
    } else if (alpha && formatOk(d, VK_FORMAT_BC3_UNORM_BLOCK)) {
        format = VK_FORMAT_BC3_UNORM_BLOCK;
        block = ImageBlock::Bc3;
    }
    if (format == VK_FORMAT_R8G8B8A8_UNORM) {
        ImageMip level{width, height, 0, static_cast<uint64_t>(width) * height * 4u};
        return submitImage(img, d, format, rgba, level.bytes, &level, 1);
    }
    return uploadBlockChain(img, d, rgba, width, height, frames, format, block, cacheDds);
}

bool gpuImageUploadPbr(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    PbrTex kind,
    const char* cacheDds) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    ImageBlock block = ImageBlock::Bc7;
    VkFormat format = VK_FORMAT_UNDEFINED;
    if (kind == PbrTex::Normal) {
        if (formatOk(d, VK_FORMAT_BC5_UNORM_BLOCK)) {
            block = ImageBlock::Bc5;
            format = VK_FORMAT_BC5_UNORM_BLOCK;
        } else if (formatOk(d, VK_FORMAT_BC7_UNORM_BLOCK)) {
            format = VK_FORMAT_BC7_UNORM_BLOCK;
        }
    } else if (kind == PbrTex::Color) {
        if (formatOk(d, VK_FORMAT_BC7_SRGB_BLOCK)) {
            format = VK_FORMAT_BC7_SRGB_BLOCK;
        } else if (formatOk(d, VK_FORMAT_BC1_RGBA_SRGB_BLOCK)) {
            block = ImageBlock::Bc1;
            format = VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        }
    } else if (formatOk(d, VK_FORMAT_BC7_UNORM_BLOCK)) {
        format = VK_FORMAT_BC7_UNORM_BLOCK;
    } else if (formatOk(d, VK_FORMAT_BC3_UNORM_BLOCK)) {
        block = ImageBlock::Bc3;
        format = VK_FORMAT_BC3_UNORM_BLOCK;
    }
    if (format == VK_FORMAT_UNDEFINED) {
        return gpuImageUploadRgba8(img, d, rgba, width, height, kind == PbrTex::Color);
    }
    return uploadBlockChain(img, d, rgba, width, height, 1, format, block, cacheDds);
}

bool gpuImageUploadRgba8(GpuImage& img, Device& d, const uint8_t* rgba, uint32_t width, uint32_t height, bool srgb) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    gpuImageDestroy(img, d);
    const VkFormat format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    std::vector<uint8_t> packed;
    std::vector<uint8_t> level(rgba, rgba + static_cast<size_t>(width) * height * 4u);
    ImageMip mips[16]{};
    uint32_t count = 0;
    uint32_t w = width;
    uint32_t h = height;
    while (count < 16u && w > 0 && h > 0) {
        mips[count].width = w;
        mips[count].height = h;
        mips[count].offset = packed.size();
        mips[count].bytes = level.size();
        packed.insert(packed.end(), level.begin(), level.end());
        count += 1u;
        if (w == 1u && h == 1u) {
            break;
        }
        std::vector<uint8_t> next;
        downsampleRgba(level, w, h, next, w, h);
        level.swap(next);
    }
    return submitImage(img, d, format, packed.data(), packed.size(), mips, count);
}

bool imageUploadUncompressed(GpuImage& img, Device& d, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    const ImageMip level{width, height, 0, static_cast<uint64_t>(width) * height * 4u};
    return submitImage(img, d, VK_FORMAT_R8G8B8A8_UNORM, rgba, level.bytes, &level, 1);
}

bool gpuImageUploadR8(GpuImage& img, Device& d, const uint8_t* pixels, uint32_t width, uint32_t height) {
    if (pixels == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    gpuImageDestroy(img, d);
    const ImageMip level{width, height, 0, static_cast<uint64_t>(width) * height};
    return submitImage(img, d, VK_FORMAT_R8_UNORM, pixels, level.bytes, &level, 1);
}

bool gpuImageUploadHdr(GpuImage& img, Device& d, const float* rgb, uint32_t width, uint32_t height) {
    if (rgb == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    if (!formatOk(d, VK_FORMAT_BC6H_UFLOAT_BLOCK)) {
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4u);
        for (uint32_t i = 0; i < width * height; ++i) {
            half[i * 4u] = imageFloatToHalf(rgb[i * 3u]);
            half[i * 4u + 1u] = imageFloatToHalf(rgb[i * 3u + 1u]);
            half[i * 4u + 2u] = imageFloatToHalf(rgb[i * 3u + 2u]);
            half[i * 4u + 3u] = imageFloatToHalf(1.0f);
        }
        ImageMip level{width, height, 0, static_cast<uint64_t>(half.size()) * 2u};
        return submitImage(img, d, VK_FORMAT_R16G16B16A16_SFLOAT, reinterpret_cast<const uint8_t*>(half.data()), level.bytes, &level, 1);
    }
    const uint32_t pw = align4(width);
    const uint32_t ph = align4(height);
    std::vector<float> padded(static_cast<size_t>(pw) * ph * 3u);
    for (uint32_t y = 0; y < ph; ++y) {
        const uint32_t sy = y < height ? y : height - 1u;
        for (uint32_t x = 0; x < pw; ++x) {
            const uint32_t sx = x < width ? x : width - 1u;
            std::memcpy(
                padded.data() + (static_cast<size_t>(y) * pw + x) * 3u,
                rgb + (static_cast<size_t>(sy) * width + sx) * 3u,
                sizeof(float) * 3u);
        }
    }
    const uint32_t mips = mipCountFor(pw, ph, true);
    ImageMip levels[16]{};
    std::vector<uint8_t> packed;
    std::vector<float> levelPixels = padded;
    uint32_t lw = pw;
    uint32_t lh = ph;
    const auto compressT0 = std::chrono::steady_clock::now();
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint32_t blocksX = lw / 4u;
        const uint32_t blocksY = lh / 4u;
        const uint64_t bytes = static_cast<uint64_t>(blocksX) * blocksY * 16u;
        levels[mip].width = lw;
        levels[mip].height = lh;
        levels[mip].offset = packed.size();
        levels[mip].bytes = bytes;
        const size_t at = packed.size();
        packed.resize(at + static_cast<size_t>(bytes));
        if (!gpuCompressLevel(d, ImageBlock::Bc6h, levelPixels.data(), lw, lh, packed.data() + at)) {
            return false;
        }
        if (mip + 1u < mips) {
            std::vector<float> next;
            downsampleRgb(levelPixels, lw, lh, next, lw, lh);
            levelPixels.swap(next);
        }
    }
    const double compressMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - compressT0).count();
    spdlog::info("hdr compress {}x{} mips {} ms {:.3f}", pw, ph, mips, compressMs);
    return submitImage(img, d, VK_FORMAT_BC6H_UFLOAT_BLOCK, packed.data(), packed.size(), levels, mips);
}

} // namespace burnhope

#include "image/ImageFile.hpp"

#include "rhi/GpuBuffer.hpp"

#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#include "ispc_texcomp.h"

namespace burnhope {
namespace {

constexpr uint32_t kMaxEdge = 4096;

bool readFile(const char* path, std::vector<uint8_t>& out) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size <= 0 || size > 64 * 1024 * 1024) {
        std::fclose(file);
        return false;
    }
    std::rewind(file);
    out.resize(static_cast<size_t>(size));
    const size_t n = std::fread(out.data(), 1, out.size(), file);
    std::fclose(file);
    return n == out.size();
}

bool isGif(const uint8_t* data, size_t n) {
    return n >= 6 && data[0] == 'G' && data[1] == 'I' && data[2] == 'F';
}

} // namespace

void imageCpuFree(ImageCpu& img) {
    std::free(img.rgba);
    std::free(img.rgb);
    img.rgba = nullptr;
    img.rgb = nullptr;
    img.width = 0;
    img.height = 0;
    img.frames = 0;
    img.hdr = 0;
}

bool imageLoadFile(ImageCpu& img, const char* path) {
    imageCpuFree(img);
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    std::vector<uint8_t> file;
    if (!readFile(path, file)) {
        spdlog::error("image read failed: {}", path);
        return false;
    }
    int w = 0;
    int h = 0;
    int comp = 0;
    int frames = 1;
    int* delays = nullptr;
    stbi_uc* pixels = nullptr;
    if (stbi_is_hdr_from_memory(file.data(), static_cast<int>(file.size())) != 0) {
        float* hdr = stbi_loadf_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 3);
        if (hdr == nullptr || w <= 0 || h <= 0) {
            spdlog::error("hdr decode failed: {} ({})", path, stbi_failure_reason());
            std::free(hdr);
            return false;
        }
        if (static_cast<uint32_t>(w) > kMaxEdge || static_cast<uint32_t>(h) > kMaxEdge) {
            spdlog::error("image too large: {}x{}", w, h);
            std::free(hdr);
            return false;
        }
        img.rgb = hdr;
        img.width = static_cast<uint32_t>(w);
        img.height = static_cast<uint32_t>(h);
        img.frames = 1;
        img.hdr = 1;
        img.delayMs[0] = 100;
        return true;
    }
    if (isGif(file.data(), file.size())) {
        int z = 0;
        pixels = stbi_load_gif_from_memory(file.data(), static_cast<int>(file.size()), &delays, &w, &h, &z, &comp, 4);
        frames = z;
    } else {
        pixels = stbi_load_from_memory(file.data(), static_cast<int>(file.size()), &w, &h, &comp, 4);
    }
    if (pixels == nullptr || w <= 0 || h <= 0 || frames <= 0) {
        spdlog::error("image decode failed: {} ({})", path, stbi_failure_reason());
        std::free(delays);
        std::free(pixels);
        return false;
    }
    if (static_cast<uint32_t>(w) > kMaxEdge || static_cast<uint32_t>(h) > kMaxEdge) {
        spdlog::error("image too large: {}x{}", w, h);
        std::free(delays);
        std::free(pixels);
        return false;
    }
    if (frames > static_cast<int>(kImageFrames)) {
        frames = static_cast<int>(kImageFrames);
    }
    const uint64_t bytes = static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * static_cast<uint64_t>(frames) * 4u;
    if (bytes > static_cast<uint64_t>(kMaxEdge) * kMaxEdge * 4u) {
        spdlog::error("image too large after frames");
        std::free(delays);
        std::free(pixels);
        return false;
    }
    img.rgba = static_cast<uint8_t*>(std::malloc(static_cast<size_t>(bytes)));
    if (img.rgba == nullptr) {
        std::free(delays);
        std::free(pixels);
        return false;
    }
    std::memcpy(img.rgba, pixels, static_cast<size_t>(bytes));
    std::free(pixels);
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.frames = static_cast<uint32_t>(frames);
    for (int i = 0; i < frames; ++i) {
        int ms = delays != nullptr ? delays[i] : 100;
        if (ms < 20) {
            ms = 100;
        }
        if (ms > 10000) {
            ms = 10000;
        }
        img.delayMs[i] = static_cast<uint16_t>(ms);
    }
    std::free(delays);
    return true;
}

namespace {

uint32_t align4(uint32_t v) {
    return (v + 3u) & ~3u;
}

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
    case VK_FORMAT_BC3_UNORM_BLOCK:
        return "BC3";
    case VK_FORMAT_BC7_UNORM_BLOCK:
        return "BC7";
    case VK_FORMAT_BC6H_UFLOAT_BLOCK:
        return "BC6H";
    case VK_FORMAT_R16G16B16A16_SFLOAT:
        return "RGBA16F";
    default:
        return "RGBA8";
    }
}

uint16_t floatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    const uint32_t mant = bits & 0x7fffffu;
    if (exp <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
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

struct Level {
    uint32_t width = 0;
    uint32_t height = 0;
    VkDeviceSize offset = 0;
    VkDeviceSize bytes = 0;
};

bool submitImage(GpuImage& img, Device& d, VkFormat format, const uint8_t* data, VkDeviceSize bytes, const Level* levels, uint32_t mipCount) {
    if (!gpuImageCreate(
            img,
            d,
            VkExtent2D{levels[0].width, levels[0].height},
            format,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo alloc{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = d.commandPool,
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
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = img.image,
        .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipCount, 0, 1},
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
    const bool ok = vkQueueSubmit2(d.graphicsQueue, 1, &submit, VK_NULL_HANDLE) == VK_SUCCESS;
    if (ok) {
        vkQueueWaitIdle(d.graphicsQueue);
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
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

void compressRgbaLevel(const uint8_t* src, uint32_t width, uint32_t height, VkFormat format, uint8_t* dst) {
    rgba_surface surf{
        const_cast<uint8_t*>(src),
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        static_cast<int32_t>(width * 4u),
    };
    if (format == VK_FORMAT_BC7_UNORM_BLOCK) {
        bc7_enc_settings settings{};
        GetProfile_alpha_fast(&settings);
        CompressBlocksBC7(&surf, dst, &settings);
    } else if (format == VK_FORMAT_BC3_UNORM_BLOCK) {
        CompressBlocksBC3(&surf, dst);
    } else {
        CompressBlocksBC1(&surf, dst);
    }
}

void compressHdrLevel(const float* rgb, uint32_t width, uint32_t height, uint8_t* dst) {
    std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4u);
    for (uint32_t i = 0; i < width * height; ++i) {
        half[i * 4u] = floatToHalf(rgb[i * 3u]);
        half[i * 4u + 1u] = floatToHalf(rgb[i * 3u + 1u]);
        half[i * 4u + 2u] = floatToHalf(rgb[i * 3u + 2u]);
        half[i * 4u + 3u] = floatToHalf(1.0f);
    }
    rgba_surface surf{
        reinterpret_cast<uint8_t*>(half.data()),
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        static_cast<int32_t>(width * 8u),
    };
    bc6h_enc_settings settings{};
    GetProfile_bc6h_fast(&settings);
    CompressBlocksBC6H(&surf, dst, &settings);
}

} // namespace

bool gpuImageUploadRgba(GpuImage& img, Device& d, const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t frames) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    if (frames == 0) {
        frames = 1;
    }
    const uint32_t frameH = height / frames;
    const bool alpha = hasAlpha(rgba, width * height);
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    if (!alpha && formatOk(d, VK_FORMAT_BC1_RGBA_UNORM_BLOCK)) {
        format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    } else if (alpha && formatOk(d, VK_FORMAT_BC7_UNORM_BLOCK)) {
        format = VK_FORMAT_BC7_UNORM_BLOCK;
    } else if (alpha && formatOk(d, VK_FORMAT_BC3_UNORM_BLOCK)) {
        format = VK_FORMAT_BC3_UNORM_BLOCK;
    }
    if (format == VK_FORMAT_R8G8B8A8_UNORM) {
        Level level{width, height, 0, static_cast<VkDeviceSize>(width) * height * 4u};
        return submitImage(img, d, format, rgba, level.bytes, &level, 1);
    }

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
    const uint32_t blockBytes = format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? 8u : 16u;
    Level levels[16]{};
    std::vector<uint8_t> packed;
    std::vector<uint8_t> levelPixels = padded;
    uint32_t lw = pw;
    uint32_t lh = stacked;
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(lw / 4u) * (lh / 4u) * blockBytes;
        levels[mip].width = lw;
        levels[mip].height = lh;
        levels[mip].offset = packed.size();
        levels[mip].bytes = bytes;
        const size_t at = packed.size();
        packed.resize(at + static_cast<size_t>(bytes));
        compressRgbaLevel(levelPixels.data(), lw, lh, format, packed.data() + at);
        if (mip + 1u < mips) {
            std::vector<uint8_t> next;
            downsampleRgba(levelPixels, lw, lh, next, lw, lh);
            levelPixels.swap(next);
        }
    }
    return submitImage(img, d, format, packed.data(), packed.size(), levels, mips);
}

bool gpuImageUploadHdr(GpuImage& img, Device& d, const float* rgb, uint32_t width, uint32_t height) {
    if (rgb == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    if (!formatOk(d, VK_FORMAT_BC6H_UFLOAT_BLOCK)) {
        std::vector<uint16_t> half(static_cast<size_t>(width) * height * 4u);
        for (uint32_t i = 0; i < width * height; ++i) {
            half[i * 4u] = floatToHalf(rgb[i * 3u]);
            half[i * 4u + 1u] = floatToHalf(rgb[i * 3u + 1u]);
            half[i * 4u + 2u] = floatToHalf(rgb[i * 3u + 2u]);
            half[i * 4u + 3u] = floatToHalf(1.0f);
        }
        Level level{width, height, 0, static_cast<VkDeviceSize>(half.size()) * 2u};
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
    Level levels[16]{};
    std::vector<uint8_t> packed;
    std::vector<float> levelPixels = padded;
    uint32_t lw = pw;
    uint32_t lh = ph;
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const uint32_t blocksX = lw / 4u;
        const uint32_t blocksY = lh / 4u;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(blocksX) * blocksY * 16u;
        levels[mip].width = lw;
        levels[mip].height = lh;
        levels[mip].offset = packed.size();
        levels[mip].bytes = bytes;
        const size_t at = packed.size();
        packed.resize(at + static_cast<size_t>(bytes));
        compressHdrLevel(levelPixels.data(), lw, lh, packed.data() + at);
        if (mip + 1u < mips) {
            std::vector<float> next;
            downsampleRgb(levelPixels, lw, lh, next, lw, lh);
            levelPixels.swap(next);
        }
    }
    return submitImage(img, d, VK_FORMAT_BC6H_UFLOAT_BLOCK, packed.data(), packed.size(), levels, mips);
}

} // namespace burnhope

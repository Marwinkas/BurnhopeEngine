#include "image/ImageFile.hpp"

#include "rhi/GpuBuffer.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Barrier.hpp"
#include "platform/Check.hpp"
#include "platform/Jobs.hpp"
#include "io/File.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
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

// Фаза 2 (план рендера): true только если устройство реально поддерживает host image copy
// И формат сам умеет через него копироваться (оптимальный тайлинг обязан объявить фичу-флаг
// в format features, не только в device features — иначе некоторые драйверы примут вызов, но
// скопируют мусор на редких форматах типа BC6H).
bool hostCopyOk(const Device& d, VkFormat format) {
    if (!d.caps.hostImageCopy) {
        return false;
    }
    VkFormatProperties3 props3{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3};
    VkFormatProperties2 props2{VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, &props3};
    vkGetPhysicalDeviceFormatProperties2(d.physical, format, &props2);
    return (props3.optimalTilingFeatures & VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT) != 0;
}

// Прямая CPU→GPU загрузка без staging-буфера, командного пула и очереди (Vulkan 1.4 core,
// `host_image_copy`). То же содержимое байт, что шло через vkCmdCopyBufferToImage — меняется
// только механизм переноса, не то, что внутри текстуры. Если что-то не так — false, и
// вызывающий код падает обратно на проверенный путь со staging-буфером, кадр не теряется.
bool submitImageHostCopy(GpuImage& img, Device& d, VkFormat format, const uint8_t* data, const Level* levels, uint32_t mipCount) {
    // Громкий отказ вместо тихой порчи памяти: regions[16] ниже фиксированного размера,
    // а mipCountFor() уже ограничивает цепочку 12 уровнями — если когда-то появится вызов с
    // другим источником mipCount, это должно упасть с трейсом тут, а не переполнить стек.
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
    spdlog::info(
        "image {} {}x{} mips {} host-copy",
        formatName(format),
        levels[0].width,
        levels[0].height,
        mipCount);
    return true;
}

bool submitImage(GpuImage& img, Device& d, VkFormat format, const uint8_t* data, VkDeviceSize bytes, const Level* levels, uint32_t mipCount) {
    BH_ASSERT(mipCount <= 16, "submitImage: mipCount exceeds fixed region array");
    if (mipCount <= 16 && hostCopyOk(d, format) && submitImageHostCopy(img, d, format, data, levels, mipCount)) {
        return true;
    }
    // TRANSFER_SRC добавлен ради читаемости текстуры задним числом (тесты, будущий дебаг-дамп
    // «что реально загружено на GPU») — ничего не стоит на десктопных GPU, в сам рендер не
    // добавляет шагов, courtesy-флаг создания образа, не новый путь копирования.
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

    // d.commandPool/d.transferPool общий разовый пул, submit — общая очередь: запись кадра
    // разных окон (job system, core/host/Host.cpp) идёт параллельно, без этой блокировки
    // allocate/free в одном пуле и submit на одной VkQueue из разных потоков — UB.
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

struct CompressFlight {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    GpuBuffer* cache = nullptr;
    uint8_t* dst = nullptr;
    VkDeviceSize bytes = 0;
};

bool gDeferWait = false;
CompressFlight gFlight{};

void gpuCompressJoin(Device& d) {
    if (gFlight.cmd == VK_NULL_HANDLE) {
        return;
    }
    std::lock_guard<std::mutex> uploadLock(d.queueMutex);
    vkQueueWaitIdle(d.graphicsQueue);
    if (gFlight.cache != nullptr && gFlight.dst != nullptr && gFlight.bytes > 0) {
        gpuBufferInvalidate(*gFlight.cache, d, 0, gFlight.bytes);
        std::memcpy(gFlight.dst, gFlight.cache->mapped, static_cast<size_t>(gFlight.bytes));
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &gFlight.cmd);
    gFlight = {};
}

// Один compute-проход на блок 4x4 (compress.slang). BC1 — 8 байт, BC7 mode 6 — 16 байт.
// Конечные точки min/max, не перебор партиций. Неудача (нет compute, нет SPIR-V) — false,
// вызывающий падает на ISPC. Сравнение с оригиналом — по декодированному цвету, не по байтам.
bool gpuCompressBlocks(
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint8_t* dst,
    uint32_t blockBytes,
    uint32_t pixelBytes,
    const char* spvPath,
    const char* tag) {
    (void)tag;
    const bool defer = gDeferWait;
    gDeferWait = false;
    const uint32_t blocksX = (width + 3u) / 4u;
    const uint32_t blocksY = (height + 3u) / 4u;
    const VkDeviceSize dstBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * blockBytes;
    const VkDeviceSize srcBytes = 16 + static_cast<VkDeviceSize>(width) * height * pixelBytes;

    struct CompressKeep {
        Device* device = nullptr;
        DescriptorHeaps heaps{};
        ShaderExt shader[8]{};
        const char* path[8]{};
        GpuBuffer src{};
        GpuBuffer dst{};
        VkDeviceSize srcCap = 0;
        VkDeviceSize dstCap = 0;
    };
    static CompressKeep keep{};
    static std::mutex keepMutex;
    std::lock_guard<std::mutex> keepLock(keepMutex);
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    bool ok = false;
    ShaderExt* cs = nullptr;

    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (keep.device != &d) {
        keep = {};
        keep.device = &d;
    }
    if (keep.srcCap < srcBytes) {
        gpuBufferDestroy(keep.src, d);
        if (!gpuBufferCreate(keep.src, d, srcBytes, usage, true)) {
            keep.srcCap = 0;
            return false;
        }
        keep.srcCap = srcBytes;
    }
    if (keep.dstCap < dstBytes) {
        gpuBufferDestroy(keep.dst, d);
        if (!gpuBufferCreate(keep.dst, d, dstBytes, usage, true)) {
            keep.dstCap = 0;
            return false;
        }
        keep.dstCap = dstBytes;
    }
    {
        uint32_t header[4] = {width, height, 0, 0};
        std::memcpy(keep.src.mapped, header, sizeof(header));
        std::memcpy(static_cast<uint8_t*>(keep.src.mapped) + 16, rgba, static_cast<size_t>(width) * height * pixelBytes);
        gpuBufferFlush(keep.src, d, 0, srcBytes);
    }
    if (keep.heaps.writeResources == nullptr
        && !descriptorHeapsCreate(keep.heaps, d, HeapLayout{.buffers = 2, .images = 0, .samplers = 0})) {
        return false;
    }
    if (!heapWriteBuffer(keep.heaps, d, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, keep.src.address, keep.src.size)
        || !heapWriteBuffer(keep.heaps, d, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, keep.dst.address, keep.dst.size)) {
        return false;
    }
    for (uint32_t i = 0; i < 8; ++i) {
        if (keep.path[i] == spvPath) {
            cs = &keep.shader[i];
            break;
        }
        if (keep.path[i] == nullptr) {
            VkDescriptorSetAndBindingMappingEXT maps[2] = {
                heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
                    heapBufOffset(keep.heaps, 0), static_cast<uint32_t>(keep.heaps.bufferDescSize)),
                heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_STORAGE_BUFFER_BIT_EXT,
                    heapBufOffset(keep.heaps, 1), static_cast<uint32_t>(keep.heaps.bufferDescSize)),
            };
            const ShaderCreateDesc desc{
                .path = spvPath,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .mappingCount = 2,
                .mappings = maps,
            };
            if (!shaderCreate(d, desc, keep.shader[i])) {
                return false;
            }
            keep.path[i] = spvPath;
            cs = &keep.shader[i];
            break;
        }
    }
    if (cs == nullptr) {
        return false;
    }
    {
        // Общий пул/очередь — см. комментарий у d.queueMutex в Device.hpp.
        std::lock_guard<std::mutex> uploadLock(d.queueMutex);
        const VkCommandBufferAllocateInfo alloc{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = d.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        if (vkAllocateCommandBuffers(d.device, &alloc, &cmd) != VK_SUCCESS) {
            return false;
        }
        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cmd, &begin);
        heapBind(cmd, keep.heaps);
        cmdBindCompute(cmd, cs->handle);
        vkCmdDispatch(cmd, (blocksX + 7u) / 8u, (blocksY + 7u) / 8u, 1);
        rhiBufferBarrier(
            cmd,
            keep.dst.buffer,
            0,
            dstBytes,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_HOST_BIT,
            VK_ACCESS_2_HOST_READ_BIT);
        vkEndCommandBuffer(cmd);

        const VkCommandBufferSubmitInfo cbsi{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cbsi,
        };
        if (vkQueueSubmit2(d.graphicsQueue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
            vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
            return false;
        }
        if (defer) {
            gFlight.cmd = cmd;
            gFlight.cache = &keep.dst;
            gFlight.dst = dst;
            gFlight.bytes = dstBytes;
            return true;
        }
        vkQueueWaitIdle(d.graphicsQueue);
        gpuBufferInvalidate(keep.dst, d, 0, dstBytes);
        std::memcpy(dst, keep.dst.mapped, static_cast<size_t>(dstBytes));
        ok = true;
    }

    if (cmd != VK_NULL_HANDLE) {
        vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
    }
    return ok;
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

void compressChannelLevel(const uint8_t* src, uint32_t width, uint32_t height, bool two, uint8_t* dst) {
    rgba_surface surf{
        const_cast<uint8_t*>(src),
        static_cast<int32_t>(width),
        static_cast<int32_t>(height),
        static_cast<int32_t>(width * 4u),
    };
    if (two) {
        CompressBlocksBC5(&surf, dst);
    } else {
        CompressBlocksBC4(&surf, dst);
    }
}

} // namespace

bool imageUploadCompressed(GpuImage& img, Device& d, VkFormat format, const uint8_t* blocks, uint32_t width, uint32_t height) {
    if (blocks == nullptr || width == 0 || height == 0) {
        return false;
    }
    Level level{};
    level.width = width;
    level.height = height;
    const uint32_t bx = (width + 3u) / 4u;
    const uint32_t by = (height + 3u) / 4u;
    const uint32_t blockBytes = format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK ? 8u : 16u;
    level.bytes = static_cast<VkDeviceSize>(bx) * by * blockBytes;
    return submitImageHostCopy(img, d, format, blocks, &level, 1);
}

bool imageLoadDds(GpuImage& img, Device& d, const char* path) {
    MappedFile mapped{};
    if (!fileMapReadOnly(path, &mapped) || mapped.size < 128) {
        return false;
    }
    const uint8_t* h = mapped.data;
    if (std::memcmp(h, "DDS ", 4) != 0) {
        fileUnmap(&mapped);
        return false;
    }
    const uint32_t height = static_cast<uint32_t>(h[12]) | (static_cast<uint32_t>(h[13]) << 8) | (static_cast<uint32_t>(h[14]) << 16) | (static_cast<uint32_t>(h[15]) << 24);
    const uint32_t width = static_cast<uint32_t>(h[16]) | (static_cast<uint32_t>(h[17]) << 8) | (static_cast<uint32_t>(h[18]) << 16) | (static_cast<uint32_t>(h[19]) << 24);
    uint32_t mips = static_cast<uint32_t>(h[28]) | (static_cast<uint32_t>(h[29]) << 8) | (static_cast<uint32_t>(h[30]) << 16) | (static_cast<uint32_t>(h[31]) << 24);
    const uint8_t* four = h + 84;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t blockBytes = 0;
    if (std::memcmp(four, "DXT1", 4) == 0) {
        format = VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        blockBytes = 8;
    } else if (std::memcmp(four, "DXT5", 4) == 0) {
        format = VK_FORMAT_BC3_UNORM_BLOCK;
        blockBytes = 16;
    } else     if (std::memcmp(four, "ATI2", 4) == 0) {
        format = VK_FORMAT_BC5_UNORM_BLOCK;
        blockBytes = 16;
    } else if (std::memcmp(four, "BC7 ", 4) == 0) {
        format = VK_FORMAT_BC7_UNORM_BLOCK;
        blockBytes = 16;
    }
    if (format == VK_FORMAT_UNDEFINED || width == 0 || height == 0) {
        fileUnmap(&mapped);
        return false;
    }
    if (mips < 1) {
        mips = 1;
    }
    if (mips > 16) {
        mips = 16;
    }
    Level levels[16]{};
    VkDeviceSize cursor = 128;
    uint32_t w = width;
    uint32_t ht = height;
    uint32_t count = 0;
    for (uint32_t i = 0; i < mips; ++i) {
        const uint32_t bx = (w + 3u) / 4u;
        const uint32_t by = (ht + 3u) / 4u;
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(bx) * by * blockBytes;
        if (cursor + bytes > mapped.size) {
            break;
        }
        levels[count].width = w;
        levels[count].height = ht;
        levels[count].offset = cursor;
        levels[count].bytes = bytes;
        cursor += bytes;
        count += 1;
        w = w > 1 ? w / 2 : 1;
        ht = ht > 1 ? ht / 2 : 1;
    }
    const bool ok = count > 0 && submitImageHostCopy(img, d, format, mapped.data, levels, count);
    fileUnmap(&mapped);
    return ok;
}

uint32_t imageBlockBytes(ImageBlock format) {
    switch (format) {
    case ImageBlock::Bc1:
    case ImageBlock::Bc4:
        return 8u;
    case ImageBlock::Bc3:
    case ImageBlock::Bc5:
    case ImageBlock::Bc6h:
    case ImageBlock::Bc7:
        return 16u;
    }
    return 0u;
}

bool gpuCompressLevel(Device& d, ImageBlock format, const void* pixels, uint32_t width, uint32_t height, uint8_t* dst) {
    if (pixels == nullptr || dst == nullptr || width == 0 || height == 0 || (width % 4u) != 0 || (height % 4u) != 0) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    switch (format) {
    case ImageBlock::Bc1:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 8u, 4u, BH_COMPRESS_BC1, "bc1")) {
            compressRgbaLevel(bytes, width, height, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, dst);
        }
        return true;
    case ImageBlock::Bc3:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 16u, 4u, BH_COMPRESS_BC3, "bc3")) {
            compressRgbaLevel(bytes, width, height, VK_FORMAT_BC3_UNORM_BLOCK, dst);
        }
        return true;
    case ImageBlock::Bc4:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 8u, 4u, BH_COMPRESS_BC4, "bc4")) {
            compressChannelLevel(bytes, width, height, false, dst);
        }
        return true;
    case ImageBlock::Bc5:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 16u, 4u, BH_COMPRESS_BC5, "bc5")) {
            compressChannelLevel(bytes, width, height, true, dst);
        }
        return true;
    case ImageBlock::Bc7:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 16u, 4u, BH_COMPRESS_BC7, "bc7")) {
            compressRgbaLevel(bytes, width, height, VK_FORMAT_BC7_UNORM_BLOCK, dst);
        }
        return true;
    case ImageBlock::Bc6h:
        if (!gpuCompressBlocks(d, bytes, width, height, dst, 16u, 12u, BH_COMPRESS_BC6H, "bc6h")) {
            compressHdrLevel(static_cast<const float*>(pixels), width, height, dst);
        }
        return true;
    }
    return false;
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

void writeBlockDds(
    const char* path,
    uint32_t width,
    uint32_t height,
    uint32_t mips,
    VkFormat format,
    const uint8_t* data,
    size_t bytes) {
    if (path == nullptr || path[0] == '\0' || data == nullptr || bytes == 0) {
        return;
    }
    const char* four = nullptr;
    if (format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK) {
        four = "DXT1";
    } else if (format == VK_FORMAT_BC3_UNORM_BLOCK) {
        four = "DXT5";
    } else if (format == VK_FORMAT_BC7_UNORM_BLOCK) {
        four = "BC7 ";
    }
    if (four == nullptr) {
        return;
    }
    mkdir("cache", 0755);
    mkdir("cache/textures", 0755);
    uint8_t hdr[128]{};
    std::memcpy(hdr, "DDS ", 4);
    const uint32_t size = 124;
    const uint32_t flags = 0x000A1007u;
    const uint32_t pfSize = 32;
    const uint32_t pfFlags = 4;
    const uint32_t caps = mips > 1 ? 0x00401008u : 0x00001000u;
    std::memcpy(hdr + 4, &size, 4);
    std::memcpy(hdr + 8, &flags, 4);
    std::memcpy(hdr + 12, &height, 4);
    std::memcpy(hdr + 16, &width, 4);
    std::memcpy(hdr + 28, &mips, 4);
    std::memcpy(hdr + 76, &pfSize, 4);
    std::memcpy(hdr + 80, &pfFlags, 4);
    std::memcpy(hdr + 84, four, 4);
    std::memcpy(hdr + 108, &caps, 4);
    std::FILE* file = std::fopen(path, "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(hdr, 1, 128, file);
    std::fwrite(data, 1, bytes, file);
    std::fclose(file);
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
    const ImageBlock block = format == VK_FORMAT_BC1_RGBA_UNORM_BLOCK
        ? ImageBlock::Bc1
        : (format == VK_FORMAT_BC7_UNORM_BLOCK ? ImageBlock::Bc7 : ImageBlock::Bc3);
    std::vector<std::vector<uint8_t>> chainPx;
    chainPx.push_back(std::move(padded));
    uint32_t lw = pw;
    uint32_t lh = stacked;
    while (chainPx.size() < mips) {
        std::vector<uint8_t> next;
        downsampleRgba(chainPx.back(), lw, lh, next, lw, lh);
        chainPx.push_back(std::move(next));
    }
    Level levels[16]{};
    std::vector<uint8_t> packed;
    lw = pw;
    lh = stacked;
    for (uint32_t mip = 0; mip < mips; ++mip) {
        const VkDeviceSize bytes = static_cast<VkDeviceSize>(lw / 4u) * (lh / 4u) * blockBytes;
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
        gDeferWait = true;
        gpuKicked = gpuCompressLevel(d, block, chainPx[0].data(), levels[0].width, levels[0].height, packed.data());
        if (!gpuKicked) {
            gDeferWait = false;
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
        jobsRunAndWait(jobs, jobCount);
    }
    if (gpuKicked) {
        gpuCompressJoin(d);
    }
    writeBlockDds(cacheDds, levels[0].width, levels[0].height, mips, format, packed.data(), packed.size());
    return submitImage(img, d, format, packed.data(), packed.size(), levels, mips);
}

bool imageUploadUncompressed(GpuImage& img, Device& d, const uint8_t* rgba, uint32_t width, uint32_t height) {
    if (rgba == nullptr || width == 0 || height == 0 || d.device == VK_NULL_HANDLE) {
        return false;
    }
    const Level level{width, height, 0, static_cast<VkDeviceSize>(width) * height * 4u};
    return submitImage(img, d, VK_FORMAT_R8G8B8A8_UNORM, rgba, level.bytes, &level, 1);
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
    const auto compressT0 = std::chrono::steady_clock::now();
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

#include "image/ImageCompress.hpp"

#include "Str.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/ShaderObject.hpp"

#include "ispc_texcomp.h"

#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace burnhope {
namespace {

struct CompressFlight {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    GpuBuffer src{};
    GpuBuffer dst{};
    uint8_t* out = nullptr;
    VkDeviceSize bytes = 0;
};

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

struct CompressPipeline {
    std::mutex mu;
    CompressKeep keep{};
    CompressFlight flight{};
};

CompressPipeline& pipeline() {
    static CompressPipeline pipe;
    return pipe;
}

void joinFlight(CompressPipeline& pipe, Device& d) {
    CompressFlight& flight = pipe.flight;
    if (flight.cmd == VK_NULL_HANDLE) {
        return;
    }
    std::lock_guard<std::mutex> uploadLock(d.queueMutex);
    vkQueueWaitIdle(d.graphicsQueue);
    if (flight.out != nullptr && flight.bytes > 0 && flight.dst.mapped != nullptr) {
        gpuBufferInvalidate(flight.dst, d, 0, flight.bytes);
        std::memcpy(flight.out, flight.dst.mapped, static_cast<size_t>(flight.bytes));
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &flight.cmd);
    gpuBufferDestroy(flight.src, d);
    gpuBufferDestroy(flight.dst, d);
    flight = {};
}

bool gpuCompressBlocks(
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint8_t* dst,
    uint32_t blockBytes,
    uint32_t pixelBytes,
    const char* spvPath,
    bool defer) {
    const uint32_t blocksX = (width + 3u) / 4u;
    const uint32_t blocksY = (height + 3u) / 4u;
    const VkDeviceSize dstBytes = static_cast<VkDeviceSize>(blocksX) * blocksY * blockBytes;
    const VkDeviceSize srcBytes = 16 + static_cast<VkDeviceSize>(width) * height * pixelBytes;
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    CompressPipeline& pipe = pipeline();
    std::lock_guard<std::mutex> keepLock(pipe.mu);
    joinFlight(pipe, d);

    CompressKeep& keep = pipe.keep;
    if (keep.device != &d) {
        keep = {};
        keep.device = &d;
    }

    GpuBuffer* src = &keep.src;
    GpuBuffer* dstBuf = &keep.dst;
    if (!defer) {
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
    } else {
        if (!gpuBufferCreate(pipe.flight.src, d, srcBytes, usage, true) || !gpuBufferCreate(pipe.flight.dst, d, dstBytes, usage, true)) {
            gpuBufferDestroy(pipe.flight.src, d);
            gpuBufferDestroy(pipe.flight.dst, d);
            pipe.flight = {};
            return false;
        }
        src = &pipe.flight.src;
        dstBuf = &pipe.flight.dst;
    }
    struct FlightGuard {
        CompressPipeline* pipe = nullptr;
        Device* device = nullptr;
        bool armed = false;
        ~FlightGuard() {
            if (!armed || pipe == nullptr) {
                return;
            }
            gpuBufferDestroy(pipe->flight.src, *device);
            gpuBufferDestroy(pipe->flight.dst, *device);
            pipe->flight = {};
        }
    } guard{&pipe, &d, defer};

    uint32_t header[4] = {width, height, 0, 0};
    std::memcpy(src->mapped, header, sizeof(header));
    std::memcpy(static_cast<uint8_t*>(src->mapped) + 16, rgba, static_cast<size_t>(width) * height * pixelBytes);
    gpuBufferFlush(*src, d, 0, srcBytes);

    if (keep.heaps.writeResources == nullptr
        && !descriptorHeapsCreate(keep.heaps, d, HeapLayout{.buffers = 2, .images = 0, .samplers = 0})) {
        return false;
    }
    if (!heapWriteBuffer(keep.heaps, d, 0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, src->address, src->size)
        || !heapWriteBuffer(keep.heaps, d, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, dstBuf->address, dstBuf->size)) {
        return false;
    }

    ShaderExt* cs = nullptr;
    for (uint32_t i = 0; i < 8; ++i) {
        if (keep.path[i] == spvPath || (keep.path[i] != nullptr && strSame(keep.path[i], spvPath))) {
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

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    {
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
            dstBuf->buffer,
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
            pipe.flight.cmd = cmd;
            pipe.flight.out = dst;
            pipe.flight.bytes = dstBytes;
            guard.armed = false;
            return true;
        }
        vkQueueWaitIdle(d.graphicsQueue);
        gpuBufferInvalidate(*dstBuf, d, 0, dstBytes);
        std::memcpy(dst, dstBuf->mapped, static_cast<size_t>(dstBytes));
    }
    vkFreeCommandBuffers(d.device, d.commandPool, 1, &cmd);
    return true;
}

const char* shaderFor(ImageBlock format) {
    switch (format) {
    case ImageBlock::Bc1:
        return BH_COMPRESS_BC1;
    case ImageBlock::Bc3:
        return BH_COMPRESS_BC3;
    case ImageBlock::Bc4:
        return BH_COMPRESS_BC4;
    case ImageBlock::Bc5:
        return BH_COMPRESS_BC5;
    case ImageBlock::Bc7:
        return BH_COMPRESS_BC7;
    case ImageBlock::Bc6h:
        return BH_COMPRESS_BC6H;
    }
    return nullptr;
}

bool runGpu(Device& d, ImageBlock format, const void* pixels, uint32_t width, uint32_t height, uint8_t* dst, bool defer) {
    if (pixels == nullptr || dst == nullptr || width == 0 || height == 0 || (width % 4u) != 0 || (height % 4u) != 0) {
        return false;
    }
    const char* spv = shaderFor(format);
    if (spv == nullptr) {
        return false;
    }
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    const uint32_t blockBytes = imageBlockBytes(format);
    const uint32_t pixelBytes = format == ImageBlock::Bc6h ? 12u : 4u;
    return gpuCompressBlocks(d, bytes, width, height, dst, blockBytes, pixelBytes, spv, defer);
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

uint16_t imageFloatToHalf(float value) {
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

void gpuCompressJoin(Device& d) {
    CompressPipeline& pipe = pipeline();
    std::lock_guard<std::mutex> keepLock(pipe.mu);
    joinFlight(pipe, d);
}

bool gpuCompressLevel(Device& d, ImageBlock format, const void* pixels, uint32_t width, uint32_t height, uint8_t* dst) {
    if (pixels == nullptr || dst == nullptr || width == 0 || height == 0 || (width % 4u) != 0 || (height % 4u) != 0) {
        return false;
    }
    if (runGpu(d, format, pixels, width, height, dst, false)) {
        return true;
    }
    const auto* bytes = static_cast<const uint8_t*>(pixels);
    switch (format) {
    case ImageBlock::Bc1:
        compressRgbaLevel(bytes, width, height, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, dst);
        return true;
    case ImageBlock::Bc3:
        compressRgbaLevel(bytes, width, height, VK_FORMAT_BC3_UNORM_BLOCK, dst);
        return true;
    case ImageBlock::Bc4:
        compressChannelLevel(bytes, width, height, false, dst);
        return true;
    case ImageBlock::Bc5:
        compressChannelLevel(bytes, width, height, true, dst);
        return true;
    case ImageBlock::Bc7:
        compressRgbaLevel(bytes, width, height, VK_FORMAT_BC7_UNORM_BLOCK, dst);
        return true;
    case ImageBlock::Bc6h:
        compressHdrLevel(static_cast<const float*>(pixels), width, height, dst);
        return true;
    }
    return false;
}

bool gpuCompressLevelDeferred(Device& d, ImageBlock format, const void* pixels, uint32_t width, uint32_t height, uint8_t* dst) {
    return runGpu(d, format, pixels, width, height, dst, true);
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
        half[i * 4u] = imageFloatToHalf(rgb[i * 3u]);
        half[i * 4u + 1u] = imageFloatToHalf(rgb[i * 3u + 1u]);
        half[i * 4u + 2u] = imageFloatToHalf(rgb[i * 3u + 2u]);
        half[i * 4u + 3u] = imageFloatToHalf(1.0f);
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

} // namespace burnhope

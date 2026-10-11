#pragma once

#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"

#include <initializer_list>

namespace burnhope {

// Адрес, storage и uniform. extraUsage — indirect и прочие флаги этого буфера.
[[nodiscard]] inline bool createDefaultGpuBuffer(
    GpuBuffer& out,
    Device& device,
    VkDeviceSize size,
    bool hostVisible = true,
    VkBufferUsageFlags extraUsage = 0) {
    const VkBufferUsageFlags usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | extraUsage;
    return gpuBufferCreate(out, device, size, usage, hostVisible);
}

inline void destroyBuffers(Device& device, std::initializer_list<GpuBuffer*> buffers) {
    for (GpuBuffer* buffer : buffers) {
        if (buffer != nullptr) {
            gpuBufferDestroy(*buffer, device);
        }
    }
}

inline void destroyImages(Device& device, std::initializer_list<GpuImage*> images) {
    for (GpuImage* image : images) {
        if (image != nullptr) {
            gpuImageDestroy(*image, device);
        }
    }
}

} // namespace burnhope

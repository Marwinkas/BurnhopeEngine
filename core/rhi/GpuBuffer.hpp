#pragma once

#include "rhi/Device.hpp"

namespace burnhope {

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkDeviceAddress address = 0;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

[[nodiscard]] bool gpuBufferCreate(
    GpuBuffer& b,
    Device& d,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    bool hostVisible);

void gpuBufferDestroy(GpuBuffer& b, Device& d);
void gpuBufferFlush(const GpuBuffer& b, Device& d, VkDeviceSize offset, VkDeviceSize size);
// Зеркало gpuBufferFlush для чтения: вызывать перед CPU-чтением mapped-памяти, которую писал
// GPU (compute readback) — память host-visible не обязана быть host-coherent.
void gpuBufferInvalidate(const GpuBuffer& b, Device& d, VkDeviceSize offset, VkDeviceSize size);

} // namespace burnhope

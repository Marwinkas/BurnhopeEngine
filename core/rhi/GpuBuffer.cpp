#include "rhi/GpuBuffer.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool gpuBufferCreate(
    GpuBuffer& b,
    Device& d,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    bool hostVisible) {
    gpuBufferDestroy(b, d);
    b.size = size;

    VkBufferCreateInfo ci{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };

    VmaAllocationCreateInfo aci{};
    aci.usage = hostVisible ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (hostVisible) {
        aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
            | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VmaAllocationInfo allocInfo{};
    if (vmaCreateBuffer(d.allocator, &ci, &aci, &b.buffer, &b.alloc, &allocInfo) != VK_SUCCESS) {
        spdlog::error("vmaCreateBuffer failed (size {})", static_cast<unsigned long long>(size));
        return false;
    }
    b.mapped = hostVisible ? allocInfo.pMappedData : nullptr;

    if ((usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0) {
        const VkBufferDeviceAddressInfo ai{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .buffer = b.buffer,
        };
        b.address = vkGetBufferDeviceAddress(d.device, &ai);
    }
    return true;
}

void gpuBufferDestroy(GpuBuffer& b, Device& d) {
    if (d.device != VK_NULL_HANDLE && b.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(d.allocator, b.buffer, b.alloc);
    }
    b = {};
}

void gpuBufferFlush(const GpuBuffer& b, Device& d, VkDeviceSize offset, VkDeviceSize size) {
    if (b.alloc == VK_NULL_HANDLE || d.allocator == VK_NULL_HANDLE || size == 0) {
        return;
    }
    vmaFlushAllocation(d.allocator, b.alloc, offset, size);
}

void gpuBufferInvalidate(const GpuBuffer& b, Device& d, VkDeviceSize offset, VkDeviceSize size) {
    if (b.alloc == VK_NULL_HANDLE || d.allocator == VK_NULL_HANDLE || size == 0) {
        return;
    }
    vmaInvalidateAllocation(d.allocator, b.alloc, offset, size);
}

} // namespace burnhope

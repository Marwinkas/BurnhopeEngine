#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"

namespace burnhope {

// Slot counts are the caller's. RHI does not name passes (vis, UI, …).
struct HeapLayout {
    uint32_t buffers = 1;
    uint32_t images = 0;
    uint32_t samplers = 0;
};

struct DescriptorHeaps {
    HeapLayout layout{};
    GpuBuffer resources;
    GpuBuffer samplers;
    VkDeviceSize bufferDescSize = 0;
    VkDeviceSize imageDescSize = 0;
    VkDeviceSize samplerDescSize = 0;
    VkDeviceSize imageHeapOffset = 0;
    VkPhysicalDeviceDescriptorHeapPropertiesEXT props{};

    PFN_vkWriteResourceDescriptorsEXT writeResources = nullptr;
    PFN_vkWriteSamplerDescriptorsEXT writeSamplers = nullptr;
    PFN_vkCmdBindResourceHeapEXT bindResourceHeap = nullptr;
    PFN_vkCmdBindSamplerHeapEXT bindSamplerHeap = nullptr;
};

[[nodiscard]] VkDeviceSize heapAlign(VkDeviceSize value, VkDeviceSize alignment);

[[nodiscard]] bool descriptorHeapsCreate(DescriptorHeaps& h, Device& d, HeapLayout layout);
void descriptorHeapsDestroy(DescriptorHeaps& h, Device& d);

[[nodiscard]] bool heapWriteBuffer(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    VkDescriptorType type,
    VkDeviceAddress address,
    VkDeviceSize size);

[[nodiscard]] bool heapWriteImage(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    VkDescriptorType type,
    const GpuImage& img,
    VkImageLayout layout,
    VkImageAspectFlags aspect);

[[nodiscard]] bool heapWriteSampler(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    const VkSamplerCreateInfo& ci);

void heapBind(VkCommandBuffer cmd, const DescriptorHeaps& h);

[[nodiscard]] uint32_t heapBufOffset(const DescriptorHeaps& h, uint32_t slot);
[[nodiscard]] uint32_t heapImgOffset(const DescriptorHeaps& h, uint32_t slot);
[[nodiscard]] uint32_t heapSampOffset(const DescriptorHeaps& h, uint32_t slot);

[[nodiscard]] VkDescriptorSetAndBindingMappingEXT heapMap(
    uint32_t set,
    uint32_t binding,
    VkSpirvResourceTypeFlagsEXT resourceMask,
    uint32_t heapOffset,
    uint32_t heapStride);

} // namespace burnhope

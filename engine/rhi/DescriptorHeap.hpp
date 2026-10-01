#pragma once

#include "rhi/Device.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"

namespace burnhope {

// Resource heap slots. Buffers packed first, then images (Sascha / spec layout).
enum class HeapBuf : uint32_t {
    Frame = 0,
    Verts = 1,
    Indices = 2,
    Count = 3,
};

enum class HeapImg : uint32_t {
    Vis = 0,
    Depth = 1,
    HdrAStorage = 2,
    HdrASampled = 3,
    HdrBStorage = 4,
    HdrBSampled = 5,
    Count = 6,
};

enum class HeapSamp : uint32_t {
    Nearest = 0,
    Linear = 1,
    Count = 2,
};

struct DescriptorHeaps {
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

[[nodiscard]] bool descriptorHeapsCreate(DescriptorHeaps& h, Device& d);
void descriptorHeapsDestroy(DescriptorHeaps& h, Device& d);

[[nodiscard]] bool heapWriteBuffer(
    DescriptorHeaps& h,
    Device& d,
    HeapBuf slot,
    VkDescriptorType type,
    VkDeviceAddress address,
    VkDeviceSize size);

[[nodiscard]] bool heapWriteImage(
    DescriptorHeaps& h,
    Device& d,
    HeapImg slot,
    VkDescriptorType type,
    const GpuImage& img,
    VkImageLayout layout,
    VkImageAspectFlags aspect);

[[nodiscard]] bool heapWriteSampler(
    DescriptorHeaps& h,
    Device& d,
    HeapSamp slot,
    const VkSamplerCreateInfo& ci);

void heapBind(VkCommandBuffer cmd, const DescriptorHeaps& h);

[[nodiscard]] uint32_t heapBufOffset(const DescriptorHeaps& h, HeapBuf slot);
[[nodiscard]] uint32_t heapImgOffset(const DescriptorHeaps& h, HeapImg slot);
[[nodiscard]] uint32_t heapSampOffset(const DescriptorHeaps& h, HeapSamp slot);

[[nodiscard]] VkDescriptorSetAndBindingMappingEXT heapMap(
    uint32_t set,
    uint32_t binding,
    VkSpirvResourceTypeFlagsEXT resourceMask,
    uint32_t heapOffset,
    uint32_t heapStride);

} // namespace burnhope

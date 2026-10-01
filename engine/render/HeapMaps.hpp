#pragma once

#include "rhi/DescriptorHeap.hpp"

namespace burnhope {

// Visbuffer frame slots. The heap itself is untyped; these indices are this pass.
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

inline void heapMapsVis(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[2]) {
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), static_cast<uint32_t>(h.bufferDescSize));
    out[1] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), static_cast<uint32_t>(h.bufferDescSize));
}

inline void heapMapsShade(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Vis)), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[3] = heapMap(0, 3, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrAStorage)), img);
    out[4] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Verts)), buf);
    out[5] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Indices)), buf);
}

inline void heapMapsSsr(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, static_cast<uint32_t>(HeapBuf::Frame)), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Vis)), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::Depth)), img);
    out[3] = heapMap(0, 4, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrASampled)), img);
    out[4] = heapMap(0, 5, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBStorage)), img);
    out[5] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), samp);
}

inline void heapMapsTonemap(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[2]) {
    out[0] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, static_cast<uint32_t>(HeapImg::HdrBSampled)), static_cast<uint32_t>(h.imageDescSize));
    out[1] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, static_cast<uint32_t>(HeapSamp::Linear)), static_cast<uint32_t>(h.samplerDescSize));
}

} // namespace burnhope

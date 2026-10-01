#pragma once

#include "rhi/DescriptorHeap.hpp"

namespace burnhope {

inline void heapMapsVis(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[2]) {
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Frame), static_cast<uint32_t>(h.bufferDescSize));
    out[1] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Verts), static_cast<uint32_t>(h.bufferDescSize));
}

inline void heapMapsShade(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Frame), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::Vis), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::Depth), img);
    out[3] = heapMap(0, 3, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::HdrAStorage), img);
    out[4] = heapMap(0, 7, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Verts), buf);
    out[5] = heapMap(0, 8, VK_SPIRV_RESOURCE_TYPE_READ_ONLY_STORAGE_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Indices), buf);
}

inline void heapMapsSsr(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[6]) {
    const uint32_t buf = static_cast<uint32_t>(h.bufferDescSize);
    const uint32_t img = static_cast<uint32_t>(h.imageDescSize);
    const uint32_t samp = static_cast<uint32_t>(h.samplerDescSize);
    out[0] = heapMap(0, 0, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(h, HeapBuf::Frame), buf);
    out[1] = heapMap(0, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::Vis), img);
    out[2] = heapMap(0, 2, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::Depth), img);
    out[3] = heapMap(0, 4, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::HdrASampled), img);
    out[4] = heapMap(0, 5, VK_SPIRV_RESOURCE_TYPE_READ_WRITE_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::HdrBStorage), img);
    out[5] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, HeapSamp::Linear), samp);
}

inline void heapMapsTonemap(const DescriptorHeaps& h, VkDescriptorSetAndBindingMappingEXT out[2]) {
    out[0] = heapMap(0, 6, VK_SPIRV_RESOURCE_TYPE_SAMPLED_IMAGE_BIT_EXT,
        heapImgOffset(h, HeapImg::HdrBSampled), static_cast<uint32_t>(h.imageDescSize));
    out[1] = heapMap(1, 1, VK_SPIRV_RESOURCE_TYPE_SAMPLER_BIT_EXT,
        heapSampOffset(h, HeapSamp::Linear), static_cast<uint32_t>(h.samplerDescSize));
}

} // namespace burnhope

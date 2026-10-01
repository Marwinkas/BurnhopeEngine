#include "rhi/DescriptorHeap.hpp"

#include <cassert>
#include <cstddef>
#include <spdlog/spdlog.h>

namespace burnhope {

VkDeviceSize heapAlign(VkDeviceSize value, VkDeviceSize alignment) {
    if (alignment <= 1) {
        return value;
    }
    return (value + alignment - 1) & ~(alignment - 1);
}

uint32_t heapBufOffset(const DescriptorHeaps& h, uint32_t slot) {
    assert(slot < h.layout.buffers);
    return static_cast<uint32_t>(h.bufferDescSize * static_cast<VkDeviceSize>(slot));
}

uint32_t heapImgOffset(const DescriptorHeaps& h, uint32_t slot) {
    assert(slot < h.layout.images);
    return static_cast<uint32_t>(
        h.imageHeapOffset + h.imageDescSize * static_cast<VkDeviceSize>(slot));
}

uint32_t heapSampOffset(const DescriptorHeaps& h, uint32_t slot) {
    assert(slot < h.layout.samplers);
    return static_cast<uint32_t>(h.samplerDescSize * static_cast<VkDeviceSize>(slot));
}

VkDescriptorSetAndBindingMappingEXT heapMap(
    uint32_t set,
    uint32_t binding,
    VkSpirvResourceTypeFlagsEXT resourceMask,
    uint32_t heapOffset,
    uint32_t heapStride) {
    VkDescriptorSetAndBindingMappingEXT m{};
    m.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
    m.descriptorSet = set;
    m.firstBinding = binding;
    m.bindingCount = 1;
    m.resourceMask = resourceMask;
    m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_CONSTANT_OFFSET_EXT;
    m.sourceData.constantOffset.heapOffset = heapOffset;
    m.sourceData.constantOffset.heapArrayStride = heapStride;
    return m;
}

bool descriptorHeapsCreate(DescriptorHeaps& h, Device& d, HeapLayout layout) {
    descriptorHeapsDestroy(h, d);
    h.layout = layout;
    h.props = d.heapProps;
    h.writeResources = reinterpret_cast<PFN_vkWriteResourceDescriptorsEXT>(
        vkGetDeviceProcAddr(d.device, "vkWriteResourceDescriptorsEXT"));
    h.writeSamplers = reinterpret_cast<PFN_vkWriteSamplerDescriptorsEXT>(
        vkGetDeviceProcAddr(d.device, "vkWriteSamplerDescriptorsEXT"));
    h.bindResourceHeap = reinterpret_cast<PFN_vkCmdBindResourceHeapEXT>(
        vkGetDeviceProcAddr(d.device, "vkCmdBindResourceHeapEXT"));
    h.bindSamplerHeap = reinterpret_cast<PFN_vkCmdBindSamplerHeapEXT>(
        vkGetDeviceProcAddr(d.device, "vkCmdBindSamplerHeapEXT"));
    if (h.writeResources == nullptr || h.writeSamplers == nullptr
        || h.bindResourceHeap == nullptr || h.bindSamplerHeap == nullptr) {
        spdlog::error("VK_EXT_descriptor_heap entry points missing");
        return false;
    }

    h.bufferDescSize = heapAlign(h.props.bufferDescriptorSize, h.props.bufferDescriptorAlignment);
    h.imageDescSize = heapAlign(h.props.imageDescriptorSize, h.props.imageDescriptorAlignment);
    h.samplerDescSize = heapAlign(h.props.samplerDescriptorSize, h.props.samplerDescriptorAlignment);
    h.imageHeapOffset = heapAlign(
        h.bufferDescSize * static_cast<VkDeviceSize>(layout.buffers),
        h.props.imageDescriptorAlignment);

    const VkDeviceSize resourcePayload = h.imageHeapOffset
        + h.imageDescSize * static_cast<VkDeviceSize>(layout.images);
    VkDeviceSize resourceSize = heapAlign(
        resourcePayload + h.props.minResourceHeapReservedRange,
        h.props.resourceHeapAlignment);
    const VkDeviceSize samplerPayload =
        h.samplerDescSize * static_cast<VkDeviceSize>(layout.samplers);
    VkDeviceSize samplerSize = heapAlign(
        samplerPayload + h.props.minSamplerHeapReservedRange,
        h.props.samplerHeapAlignment);
    if (resourceSize == 0) {
        resourceSize = 256;
    }
    if (samplerSize == 0) {
        samplerSize = 256;
    }

    const VkBufferUsageFlags heapUsage =
        VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (!gpuBufferCreate(h.resources, d, resourceSize, heapUsage, true)
        || !gpuBufferCreate(h.samplers, d, samplerSize, heapUsage, true)) {
        spdlog::error("descriptor heap buffers failed");
        descriptorHeapsDestroy(h, d);
        return false;
    }

    spdlog::info(
        "descriptor heaps: resource {} B (bufDesc {} imgDesc {} imgOff {}) sampler {} B (sampDesc {})",
        static_cast<unsigned long long>(resourceSize),
        static_cast<unsigned long long>(h.bufferDescSize),
        static_cast<unsigned long long>(h.imageDescSize),
        static_cast<unsigned long long>(h.imageHeapOffset),
        static_cast<unsigned long long>(samplerSize),
        static_cast<unsigned long long>(h.samplerDescSize));
    return true;
}

void descriptorHeapsDestroy(DescriptorHeaps& h, Device& d) {
    gpuBufferDestroy(h.resources, d);
    gpuBufferDestroy(h.samplers, d);
    h.writeResources = nullptr;
    h.writeSamplers = nullptr;
    h.bindResourceHeap = nullptr;
    h.bindSamplerHeap = nullptr;
}

bool heapWriteBuffer(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    VkDescriptorType type,
    VkDeviceAddress address,
    VkDeviceSize size) {
    if (h.resources.mapped == nullptr || h.writeResources == nullptr) {
        return false;
    }
    const VkDeviceAddressRangeEXT range{.address = address, .size = size};
    const VkResourceDescriptorInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .type = type,
        .data = {.pAddressRange = &range},
    };
    const VkHostAddressRangeEXT dest{
        .address = static_cast<std::byte*>(h.resources.mapped) + heapBufOffset(h, slot),
        .size = h.bufferDescSize,
    };
    const VkResult r = h.writeResources(d.device, 1, &info, &dest);
    if (r != VK_SUCCESS) {
        spdlog::error("vkWriteResourceDescriptorsEXT buffer failed: {}", static_cast<int>(r));
        return false;
    }
    return true;
}

bool heapWriteImage(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    VkDescriptorType type,
    const GpuImage& img,
    VkImageLayout layout,
    VkImageAspectFlags aspect) {
    if (h.resources.mapped == nullptr || h.writeResources == nullptr) {
        return false;
    }
    const VkImageViewCreateInfo viewCi{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = img.image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = img.format,
        .subresourceRange = {aspect, 0, 1, 0, 1},
    };
    const VkImageDescriptorInfoEXT imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT,
        .pView = &viewCi,
        .layout = layout,
    };
    const VkResourceDescriptorInfoEXT info{
        .sType = VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT,
        .type = type,
        .data = {.pImage = &imageInfo},
    };
    const VkHostAddressRangeEXT dest{
        .address = static_cast<std::byte*>(h.resources.mapped) + heapImgOffset(h, slot),
        .size = h.imageDescSize,
    };
    const VkResult r = h.writeResources(d.device, 1, &info, &dest);
    if (r != VK_SUCCESS) {
        spdlog::error("vkWriteResourceDescriptorsEXT image failed: {}", static_cast<int>(r));
        return false;
    }
    return true;
}

bool heapWriteSampler(
    DescriptorHeaps& h,
    Device& d,
    uint32_t slot,
    const VkSamplerCreateInfo& ci) {
    if (h.samplers.mapped == nullptr || h.writeSamplers == nullptr) {
        return false;
    }
    const VkHostAddressRangeEXT dest{
        .address = static_cast<std::byte*>(h.samplers.mapped) + heapSampOffset(h, slot),
        .size = h.samplerDescSize,
    };
    const VkResult r = h.writeSamplers(d.device, 1, &ci, &dest);
    if (r != VK_SUCCESS) {
        spdlog::error("vkWriteSamplerDescriptorsEXT failed: {}", static_cast<int>(r));
        return false;
    }
    return true;
}

void heapBind(VkCommandBuffer cmd, const DescriptorHeaps& h) {
    const VkBindHeapInfoEXT res{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .heapRange = {.address = h.resources.address, .size = h.resources.size},
        .reservedRangeOffset = h.resources.size - h.props.minResourceHeapReservedRange,
        .reservedRangeSize = h.props.minResourceHeapReservedRange,
    };
    const VkBindHeapInfoEXT samp{
        .sType = VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT,
        .heapRange = {.address = h.samplers.address, .size = h.samplers.size},
        .reservedRangeOffset = h.samplers.size - h.props.minSamplerHeapReservedRange,
        .reservedRangeSize = h.props.minSamplerHeapReservedRange,
    };
    h.bindResourceHeap(cmd, &res);
    h.bindSamplerHeap(cmd, &samp);
}

} // namespace burnhope

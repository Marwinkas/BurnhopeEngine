#include "ResourceHeap.hpp"
#include "VkExtDispatch.hpp"
#include <algorithm>
#include <cstring>

namespace burnhope {

ResourceHeap::ResourceHeap(BurnhopeDevice& device, VkDeviceSize resourceBytes, VkDeviceSize samplerBytes)
    : device_{device} {
  const auto& hp = device_.caps().heapProps;
  resourceBytes_ = std::min(resourceBytes, hp.maxResourceHeapSize);
  samplerBytes_ = std::min(samplerBytes, hp.maxSamplerHeapSize);
  resourceReserved_ = hp.minResourceHeapReservedRange;
  samplerReserved_ = hp.minSamplerHeapReservedRange;
  resourceStride_ = device_.resourceHeapDescriptorStride();
  samplerStride_ = device_.descriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER);
  if (samplerStride_ == 0) {
    throwVkError("ResourceHeap: invalid sampler descriptor stride");
  }
  resourceOffset_ = resourceReserved_;
  samplerOffset_ = samplerReserved_;

  device_.createBuffer(
      resourceBytes_,
      VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      resourceBuffer_,
      resourceAlloc_);

  if (samplerBytes_ > 0) {
    device_.createBuffer(
        samplerBytes_,
        VK_BUFFER_USAGE_DESCRIPTOR_HEAP_BIT_EXT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        samplerBuffer_,
        samplerAlloc_);
    vmaMapMemory(device_.allocator(), samplerAlloc_, &samplerHost_);
    samplerGpuAddress_ = device_.bufferDeviceAddress(samplerBuffer_);
  }

  vmaMapMemory(device_.allocator(), resourceAlloc_, &resourceHost_);
  resourceGpuAddress_ = device_.bufferDeviceAddress(resourceBuffer_);
}

ResourceHeap::~ResourceHeap() {
  if (resourceHost_) {
    vmaUnmapMemory(device_.allocator(), resourceAlloc_);
  }
  if (samplerHost_) {
    vmaUnmapMemory(device_.allocator(), samplerAlloc_);
  }
  if (resourceBuffer_) {
    vmaDestroyBuffer(device_.allocator(), resourceBuffer_, resourceAlloc_);
  }
  if (samplerBuffer_) {
    vmaDestroyBuffer(device_.allocator(), samplerBuffer_, samplerAlloc_);
  }
}

VkDeviceSize ResourceHeap::allocateResourceSlot(VkDeviceSize descriptorSize) {
  const VkDeviceSize aligned = static_cast<VkDeviceSize>(alignUp(resourceOffset_, resourceStride_));
  if (aligned + descriptorSize > resourceBytes_) {
    throwVkError("ResourceHeap: resource heap full");
  }
  const VkDeviceSize offset = aligned;
  resourceOffset_ = aligned + descriptorSize;
  return offset;
}

VkDeviceSize ResourceHeap::allocateSamplerSlot(VkDeviceSize descriptorSize) {
  const VkDeviceSize aligned = static_cast<VkDeviceSize>(alignUp(samplerOffset_, samplerStride_));
  if (aligned + descriptorSize > samplerBytes_) {
    throwVkError("ResourceHeap: sampler heap full");
  }
  const VkDeviceSize offset = aligned;
  samplerOffset_ = aligned + descriptorSize;
  return offset;
}

VkDeviceSize ResourceHeap::writeBuffer(
    VkBuffer buffer,
    VkDeviceSize offset,
    VkDeviceSize range,
    VkFormat texelFormat,
    VkDescriptorType bufferType) {
  const VkDeviceSize heapOffset = allocateResourceSlot(resourceStride_);

  VkDeviceAddressRangeEXT addrRange{};
  addrRange.address = device_.bufferDeviceAddress(buffer) + offset;
  addrRange.size = range;

  VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
  if (texelFormat != VK_FORMAT_UNDEFINED) {
    info.type = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    VkTexelBufferDescriptorInfoEXT texel{VK_STRUCTURE_TYPE_TEXEL_BUFFER_DESCRIPTOR_INFO_EXT};
    texel.format = texelFormat;
    texel.addressRange = addrRange;
    info.data.pTexelBuffer = &texel;
  } else {
    info.type = bufferType;
    info.data.pAddressRange = &addrRange;
  }

  VkHostAddressRangeEXT hostRange{};
  hostRange.address = static_cast<std::byte*>(resourceHost_) + heapOffset;
  hostRange.size = static_cast<std::size_t>(resourceStride_);

  vkext::get().writeResourceDescriptorsEXT(device_.device(), 1, &info, &hostRange);
  return heapOffset;
}

VkDeviceSize ResourceHeap::writeImage(
    const VkImageViewCreateInfo& viewCreateInfo,
    VkImageLayout layout,
    VkDescriptorType type) {
  const VkDeviceSize heapOffset = allocateResourceSlot(resourceStride_);

  VkImageDescriptorInfoEXT imageInfo{VK_STRUCTURE_TYPE_IMAGE_DESCRIPTOR_INFO_EXT};
  imageInfo.pView = &viewCreateInfo;
  imageInfo.layout = layout;

  VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
  info.type = type;
  info.data.pImage = &imageInfo;

  VkHostAddressRangeEXT hostRange{};
  hostRange.address = static_cast<std::byte*>(resourceHost_) + heapOffset;
  hostRange.size = static_cast<std::size_t>(resourceStride_);

  vkext::get().writeResourceDescriptorsEXT(device_.device(), 1, &info, &hostRange);
  return heapOffset;
}

VkDeviceSize ResourceHeap::writeSampler(const VkSamplerCreateInfo& createInfo) {
  const VkDeviceSize heapOffset = allocateSamplerSlot(samplerStride_);

  VkHostAddressRangeEXT hostRange{};
  hostRange.address = static_cast<std::byte*>(samplerHost_) + heapOffset;
  hostRange.size = static_cast<std::size_t>(samplerStride_);

  vkext::get().writeSamplerDescriptorsEXT(device_.device(), 1, &createInfo, &hostRange);
  return heapOffset;
}

VkDeviceSize ResourceHeap::writeAccelerationStructure(VkAccelerationStructureKHR as) {
  const VkDeviceSize heapOffset = allocateResourceSlot(resourceStride_);

  VkAccelerationStructureDeviceAddressInfoKHR addrInfo{
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR};
  addrInfo.accelerationStructure = as;
  static PFN_vkGetAccelerationStructureDeviceAddressKHR pfnAddress = nullptr;
  if (!pfnAddress) {
    pfnAddress = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        vkGetDeviceProcAddr(device_.device(), "vkGetAccelerationStructureDeviceAddressKHR"));
  }
  if (!pfnAddress) {
    throwVkError("ResourceHeap: vkGetAccelerationStructureDeviceAddressKHR");
  }
  const VkDeviceAddress asAddress = pfnAddress(device_.device(), &addrInfo);

  VkDeviceAddressRangeEXT range{};
  range.address = asAddress;
  range.size = 0;

  VkResourceDescriptorInfoEXT info{VK_STRUCTURE_TYPE_RESOURCE_DESCRIPTOR_INFO_EXT};
  info.type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  info.data.pAddressRange = &range;

  VkHostAddressRangeEXT hostRange{};
  hostRange.address = static_cast<std::byte*>(resourceHost_) + heapOffset;
  hostRange.size = static_cast<std::size_t>(resourceStride_);

  vkext::get().writeResourceDescriptorsEXT(device_.device(), 1, &info, &hostRange);
  return heapOffset;
}

ResourceHeap::StorageBufferVerifyResult ResourceHeap::verifyStorageBufferDescriptor(
    VkDeviceSize heapByteOffset,
    VkDeviceAddress expectedAddress) const noexcept {
  StorageBufferVerifyResult result{};
  result.expected = expectedAddress;
  if (!resourceHost_ || resourceStride_ == 0) {
    return result;
  }
  const auto* base = static_cast<const std::byte*>(resourceHost_) + heapByteOffset;
  for (VkDeviceSize off = 0; off + sizeof(VkDeviceAddress) <= resourceStride_; off += sizeof(uint32_t)) {
    VkDeviceAddress candidate{};
    std::memcpy(&candidate, base + off, sizeof(candidate));
    if (candidate == expectedAddress) {
      result.addressMatch = true;
      result.found = candidate;
      result.matchByteOffset = static_cast<uint32_t>(off);
      return result;
    }
  }
  std::memcpy(&result.found, base, sizeof(result.found));
  return result;
}

void ResourceHeap::bind(VkCommandBuffer cmd) const {
  VkBindHeapInfoEXT resourceBind{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
  resourceBind.heapRange.address = resourceGpuAddress_;
  resourceBind.heapRange.size = resourceBytes_;
  resourceBind.reservedRangeOffset = 0;
  resourceBind.reservedRangeSize = resourceReserved_;
  vkext::get().cmdBindResourceHeapEXT(cmd, &resourceBind);

  if (samplerGpuAddress_ != 0) {
    VkBindHeapInfoEXT samplerBind{VK_STRUCTURE_TYPE_BIND_HEAP_INFO_EXT};
    samplerBind.heapRange.address = samplerGpuAddress_;
    samplerBind.heapRange.size = samplerBytes_;
    samplerBind.reservedRangeOffset = 0;
    samplerBind.reservedRangeSize = samplerReserved_;
    vkext::get().cmdBindSamplerHeapEXT(cmd, &samplerBind);
  }
}

} // namespace burnhope

#pragma once

#include "Device.hpp"
#include "Core/Types.hpp"
#include <span>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

/**
 * VK_EXT_descriptor_heap bindless resource heap (BDA-backed).
 * Slot index = byteOffset / descriptorStride (for Slang DescriptorHandle).
 */
class ResourceHeap final : public NonCopyable {
public:
  static constexpr VkDeviceSize kDefaultResourceBytes = 64 * 1024 * 1024;
  static constexpr VkDeviceSize kDefaultSamplerBytes = 4 * 1024 * 1024;

  ResourceHeap(BurnhopeDevice& device,
               VkDeviceSize resourceBytes = kDefaultResourceBytes,
               VkDeviceSize samplerBytes = kDefaultSamplerBytes);
  ~ResourceHeap();

  [[nodiscard]] VkDeviceAddress resourceHeapAddress() const noexcept { return resourceGpuAddress_; }
  [[nodiscard]] VkDeviceAddress samplerHeapAddress() const noexcept { return samplerGpuAddress_; }
  [[nodiscard]] VkDeviceSize resourceStride() const noexcept { return resourceStride_; }
  [[nodiscard]] VkDeviceSize samplerStride() const noexcept { return samplerStride_; }

  /** Returns byte offset in resource heap. */
  [[nodiscard]] VkDeviceSize writeBuffer(
      VkBuffer buffer,
      VkDeviceSize offset,
      VkDeviceSize range,
      VkFormat texelFormat = VK_FORMAT_UNDEFINED,
      VkDescriptorType bufferType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

  [[nodiscard]] VkDeviceSize writeImage(
      const VkImageViewCreateInfo& viewCreateInfo,
      VkImageLayout layout,
      VkDescriptorType type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

  [[nodiscard]] VkDeviceSize writeSampler(const VkSamplerCreateInfo& createInfo);

  /** AS descriptor via device address (VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR). */
  [[nodiscard]] VkDeviceSize writeAccelerationStructure(VkAccelerationStructureKHR as);

  void bind(VkCommandBuffer cmd) const;

  /** Rewrites resource descriptors after the heap reserved range (GPU idle only). */
  void resetResourceDescriptors() noexcept {
    resourceOffset_ = resourceReserved_;
    samplerOffset_ = samplerReserved_;
  }

  /** CPU readback: find expected BDA inside a heap slot (debug / minimal path). */
  struct StorageBufferVerifyResult final {
    bool addressMatch{false};
    VkDeviceAddress expected{0};
    VkDeviceAddress found{0};
    uint32_t matchByteOffset{UINT32_MAX};
  };
  [[nodiscard]] StorageBufferVerifyResult verifyStorageBufferDescriptor(
      VkDeviceSize heapByteOffset,
      VkDeviceAddress expectedAddress) const noexcept;

  [[nodiscard]] const void* resourceHostBytes() const noexcept { return resourceHost_; }

private:
  [[nodiscard]] VkDeviceSize allocateResourceSlot(VkDeviceSize descriptorSize);
  [[nodiscard]] VkDeviceSize allocateSamplerSlot(VkDeviceSize descriptorSize);

  BurnhopeDevice& device_;
  VkBuffer resourceBuffer_{VK_NULL_HANDLE};
  VmaAllocation resourceAlloc_{VK_NULL_HANDLE};
  VkBuffer samplerBuffer_{VK_NULL_HANDLE};
  VmaAllocation samplerAlloc_{VK_NULL_HANDLE};
  void* resourceHost_{nullptr};
  void* samplerHost_{nullptr};
  VkDeviceAddress resourceGpuAddress_{0};
  VkDeviceAddress samplerGpuAddress_{0};
  VkDeviceSize resourceBytes_{0};
  VkDeviceSize samplerBytes_{0};
  VkDeviceSize resourceStride_{0};
  VkDeviceSize samplerStride_{0};
  VkDeviceSize resourceOffset_{0};
  VkDeviceSize samplerOffset_{0};
  VkDeviceSize resourceReserved_{0};
  VkDeviceSize samplerReserved_{0};
};

} // namespace burnhope

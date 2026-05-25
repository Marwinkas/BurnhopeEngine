#pragma once

#include "AssetPool.hpp"
#include "Device.hpp"
#include "ResourceHeap.hpp"
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

/** Per-frame heap slot indices pushed to shaders (DescriptorHandle index). */
struct FrameBindSlots final {
  uint32_t globalUbo[3]{0, 0, 0};
  uint32_t objectStorage{0};
  uint32_t materialStorage{0};
  uint32_t boneStorage{0};
  uint32_t textureTableBase{0};
  uint32_t textureTableCount{0};
  uint32_t gbufferNormal{0};
  uint32_t gbufferAlbedo{0};
  uint32_t gbufferHeightAo{0};
  uint32_t gbufferEmissive{0};
  uint32_t gbufferPortalId{0};
  uint32_t gbufferDepth{0};
  uint32_t hdrOutput{0};
  uint32_t hdrOutputSampled{0};
  uint32_t gtaoOutput{0};
  uint32_t ssgiRaw{0};
  uint32_t postProcess{0};
  uint32_t taaHistory{0};
  uint32_t taaResolved{0};
  uint32_t exposureBuffer{0};
  uint32_t lightBuffer{0};
  uint32_t decalBuffer{0};
  uint32_t shadowCsm{0};
  uint32_t shadowAtlas{0};
  uint32_t vsmAtlas{0};
  uint32_t vsmPageTable{0};
  uint32_t vsmAllocator{0};
  uint32_t giDiffuse{0};
  uint32_t giSpecular{0};
  uint32_t giCascade0{0};
  uint32_t rtReflections{0};
  uint32_t rtTlas{0};
  uint32_t volumetric{0};
  uint32_t volumetricStorage{0};
  uint32_t hiZ{0};
  uint32_t defaultSampler{0};
  uint32_t blueNoise{0};
  uint32_t lightGrid{0};
  uint32_t lightIndexList{0};
  uint32_t faceMatrices{0};
  uint32_t portalUbos{0};
};

/**
 * VK_EXT_descriptor_heap registry (no VkDescriptorSet / pools on render path).
 * Shaders: Slang DescriptorHandle + push constants with slot indices.
 */
class BindlessRegistry final : public NonCopyable {
public:
  explicit BindlessRegistry(BurnhopeDevice& device);
  ~BindlessRegistry() = default;

  [[nodiscard]] ResourceHeap& heap() noexcept { return heap_; }
  [[nodiscard]] const ResourceHeap& heap() const noexcept { return heap_; }
  [[nodiscard]] TexturePool& textures() noexcept { return textures_; }

  [[nodiscard]] uint32_t registerUniformBuffer(
      VkBuffer buffer,
      VkDeviceSize offset,
      VkDeviceSize range);
  [[nodiscard]] uint32_t registerStorageBuffer(
      VkBuffer buffer,
      VkDeviceSize offset,
      VkDeviceSize range);
  [[nodiscard]] uint32_t registerStorageImage(
      BurnhopeTexture& texture,
      VkImageLayout layout = VK_IMAGE_LAYOUT_GENERAL);
  [[nodiscard]] uint32_t registerSampledImage(
      BurnhopeTexture& texture,
      VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  [[nodiscard]] uint32_t registerSampler(VkSamplerCreateInfo createInfo);
  [[nodiscard]] uint32_t registerAccelerationStructure(VkAccelerationStructureKHR as);

  void bind(VkCommandBuffer cmd) const;
  void resetResourceHeap() noexcept { heap_.resetResourceDescriptors(); }
  void pushHeapData(VkCommandBuffer cmd, const void* data, uint32_t size) const;
  void pushHeapDataAt(VkCommandBuffer cmd, uint32_t byteOffset, const void* data, uint32_t size) const;
  void pushAndBind(VkCommandBuffer cmd, const void* data, uint32_t size) const;

  [[nodiscard]] FrameBindSlots& slots() noexcept { return slots_; }
  [[nodiscard]] const FrameBindSlots& slots() const noexcept { return slots_; }

  /** Compare heap slot bytes vs vkGetBufferDeviceAddress(buffer)+offset (CPU debug). */
  [[nodiscard]] bool verifyStorageBufferSlot(
      uint32_t slot,
      VkBuffer buffer,
      VkDeviceSize bufferOffset,
      const char* label) const noexcept;

private:
  [[nodiscard]] uint32_t byteOffsetToSlot(VkDeviceSize byteOffset) const noexcept;

  BurnhopeDevice& device_;
  ResourceHeap heap_;
  TexturePool textures_;
  FrameBindSlots slots_{};
  VkSamplerCreateInfo defaultSamplerInfo_{};
};

} // namespace burnhope

#pragma once

#include "Device.hpp"
#include <span>
#include <vulkan/vulkan.h>

namespace burnhope {

/** SoA-friendly GPU buffer with optional CPU map (upload path only). */
class BurnhopeBuffer final : public NonCopyable {
public:
  BurnhopeBuffer(
      BurnhopeDevice& device,
      VkDeviceSize instanceSize,
      uint32_t instanceCount,
      VkBufferUsageFlags usageFlags,
      VkMemoryPropertyFlags memoryPropertyFlags,
      VkDeviceSize minOffsetAlignment = 1);

  ~BurnhopeBuffer();

  [[nodiscard]] VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void write(std::span<const std::byte> data, VkDeviceSize offset = 0);
  void writeToIndex(std::span<const std::byte> data, uint32_t index);

  [[nodiscard]] VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  [[nodiscard]] VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  [[nodiscard]] VkResult flushIndex(uint32_t index);
  [[nodiscard]] VkResult invalidateIndex(uint32_t index);

  /** Legacy descriptor-set path (transitional; prefer ResourceHeap offsets). */
  [[nodiscard]] VkDescriptorBufferInfo descriptorInfo(
      VkDeviceSize size = VK_WHOLE_SIZE,
      VkDeviceSize offset = 0) const;
  [[nodiscard]] VkDescriptorBufferInfo descriptorInfoForIndex(uint32_t index) const;

  [[nodiscard]] VkBuffer handle() const noexcept { return buffer_; }
  [[nodiscard]] VkDeviceAddress deviceAddress() const;
  [[nodiscard]] void* mappedMemory() const noexcept { return mapped_; }
  [[nodiscard]] uint32_t instanceCount() const noexcept { return instanceCount_; }
  [[nodiscard]] VkDeviceSize instanceSize() const noexcept { return instanceSize_; }
  [[nodiscard]] VkDeviceSize alignmentSize() const noexcept { return alignmentSize_; }
  [[nodiscard]] VkDeviceSize bufferSize() const noexcept { return bufferSize_; }

  // Legacy aliases
  [[nodiscard]] VkBuffer getBuffer() const noexcept { return handle(); }
  [[nodiscard]] void* getMappedMemory() const noexcept { return mappedMemory(); }
  [[nodiscard]] uint32_t getInstanceCount() const noexcept { return instanceCount(); }
  [[nodiscard]] VkDeviceSize getInstanceSize() const noexcept { return instanceSize(); }
  [[nodiscard]] VkDeviceSize getAlignmentSize() const noexcept { return alignmentSize(); }
  [[nodiscard]] VkBufferUsageFlags getUsageFlags() const noexcept { return usageFlags_; }
  [[nodiscard]] VkMemoryPropertyFlags getMemoryPropertyFlags() const noexcept {
    return memoryPropertyFlags_;
  }
  [[nodiscard]] VkDeviceSize getBufferSize() const noexcept { return bufferSize(); }
  void writeToBuffer(void* data, VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) {
    write({static_cast<const std::byte*>(data), size == VK_WHOLE_SIZE ? bufferSize_ : size}, offset);
  }
  void writeToIndex(void* data, int index) {
    writeToIndex({static_cast<const std::byte*>(data), instanceSize_},
                 static_cast<uint32_t>(index));
  }
  [[nodiscard]] VkResult flushIndex(int index) { return flushIndex(static_cast<uint32_t>(index)); }

private:
  [[nodiscard]] static VkDeviceSize alignInstance(VkDeviceSize instanceSize, VkDeviceSize alignment);

  BurnhopeDevice& device_;
  void* mapped_{nullptr};
  VkBuffer buffer_{VK_NULL_HANDLE};
  VmaAllocation memory_{VK_NULL_HANDLE};
  VkDeviceSize bufferSize_{0};
  uint32_t instanceCount_{0};
  VkDeviceSize instanceSize_{0};
  VkDeviceSize alignmentSize_{0};
  VkBufferUsageFlags usageFlags_{0};
  VkMemoryPropertyFlags memoryPropertyFlags_{0};
};

} // namespace burnhope

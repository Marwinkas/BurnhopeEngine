#include "Buffer.hpp"
#include <cstring>

namespace burnhope {

VkDeviceSize BurnhopeBuffer::alignInstance(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment) {
  if (minOffsetAlignment == 0) {
    return instanceSize;
  }
  return static_cast<VkDeviceSize>(alignUp(instanceSize, minOffsetAlignment));
}

BurnhopeBuffer::BurnhopeBuffer(
    BurnhopeDevice& device,
    VkDeviceSize instanceSize,
    uint32_t instanceCount,
    VkBufferUsageFlags usageFlags,
    VkMemoryPropertyFlags memoryPropertyFlags,
    VkDeviceSize minOffsetAlignment)
    : device_{device},
      instanceSize_{instanceSize},
      instanceCount_{instanceCount},
      usageFlags_{usageFlags},
      memoryPropertyFlags_{memoryPropertyFlags} {
  alignmentSize_ = alignInstance(instanceSize, minOffsetAlignment);
  bufferSize_ = alignmentSize_ * instanceCount;
  device_.createBuffer(bufferSize_, usageFlags, memoryPropertyFlags, buffer_, memory_);
}

BurnhopeBuffer::~BurnhopeBuffer() {
  unmap();
  vmaDestroyBuffer(device_.allocator(), buffer_, memory_);
}

VkResult BurnhopeBuffer::map(VkDeviceSize, VkDeviceSize) {
  return vmaMapMemory(device_.allocator(), memory_, &mapped_);
}

void BurnhopeBuffer::unmap() {
  if (mapped_) {
    vmaUnmapMemory(device_.allocator(), memory_);
    mapped_ = nullptr;
  }
}

void BurnhopeBuffer::write(std::span<const std::byte> data, VkDeviceSize offset) {
  if (!mapped_) {
    return;
  }
  const VkDeviceSize size = data.size();
  if (offset + size > bufferSize_) {
    throwVkError("BurnhopeBuffer::write overflow");
  }
  std::memcpy(static_cast<std::byte*>(mapped_) + offset, data.data(), size);
}

void BurnhopeBuffer::writeToIndex(std::span<const std::byte> data, uint32_t index) {
  write(data, index * alignmentSize_);
}

VkResult BurnhopeBuffer::flush(VkDeviceSize size, VkDeviceSize offset) {
  return vmaFlushAllocation(device_.allocator(), memory_, offset, size);
}

VkResult BurnhopeBuffer::invalidate(VkDeviceSize size, VkDeviceSize offset) {
  return vmaInvalidateAllocation(device_.allocator(), memory_, offset, size);
}

VkResult BurnhopeBuffer::flushIndex(uint32_t index) {
  return flush(alignmentSize_, index * alignmentSize_);
}

VkResult BurnhopeBuffer::invalidateIndex(uint32_t index) {
  return invalidate(alignmentSize_, index * alignmentSize_);
}

VkDescriptorBufferInfo BurnhopeBuffer::descriptorInfo(VkDeviceSize size, VkDeviceSize offset) const {
  return {buffer_, offset, size};
}

VkDescriptorBufferInfo BurnhopeBuffer::descriptorInfoForIndex(uint32_t index) const {
  return descriptorInfo(alignmentSize_, index * alignmentSize_);
}

VkDeviceAddress BurnhopeBuffer::deviceAddress() const {
  return device_.bufferDeviceAddress(buffer_);
}

} // namespace burnhope

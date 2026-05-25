#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "Core/ComputeDispatch.hpp"
#include <memory>
#include <vulkan/vulkan.h>

namespace burnhope {

class HiZSystem final {
public:
  HiZSystem(BurnhopeDevice& device, BindlessRegistry& bindless, uint32_t maxObjects);
  ~HiZSystem();

  void build(VkExtent2D extent, VkImageView depthView, VkSampler depthSampler);
  void dispatch(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      uint32_t depthInHeapIndex,
      uint32_t hiZOutHeapIndex,
      VkExtent2D extent);
  [[nodiscard]] uint32_t hiZHeapIndex() const noexcept { return hiZHeapIndex_; }

private:
  BurnhopeDevice& device_;
  BindlessRegistry& bindless_;
  std::unique_ptr<ComputeDispatch> shader_;
  uint32_t hiZHeapIndex_{0};
  VkImage hiZImage_{VK_NULL_HANDLE};
  VmaAllocation hiZMemory_{VK_NULL_HANDLE};
  VkImageView fullView_{VK_NULL_HANDLE};
  VkSampler hiZSampler_{VK_NULL_HANDLE};
};

} // namespace burnhope

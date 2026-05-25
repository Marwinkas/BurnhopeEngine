#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "../Utils/FrameInfo.hpp"
#include "Core/ComputeDispatch.hpp"
#include <memory>

namespace burnhope {

class DeferredLightingSystem final {
public:
  explicit DeferredLightingSystem(BurnhopeDevice& device);
  ~DeferredLightingSystem();

  DeferredLightingSystem(const DeferredLightingSystem&) = delete;
  DeferredLightingSystem& operator=(const DeferredLightingSystem&) = delete;

  void computeLighting(
      VkCommandBuffer commandBuffer,
      const BindlessRegistry& bindless,
      const LightingHeapPC& pc,
      uint32_t width,
      uint32_t height);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<ComputeDispatch> shader_;
};

} // namespace burnhope

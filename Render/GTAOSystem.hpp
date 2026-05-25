#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "Core/ComputeDispatch.hpp"
#include <memory>

namespace burnhope {

class GTAOSystem final {
public:
  GTAOSystem(BurnhopeDevice& device);
  ~GTAOSystem();

  void compute(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const GtaoHeapPC& pc,
      uint32_t width,
      uint32_t height);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<ComputeDispatch> shader_;
};

} // namespace burnhope

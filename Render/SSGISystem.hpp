#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "Core/ComputeDispatch.hpp"
#include <memory>

namespace burnhope {

class SSGISystem final {
public:
  SSGISystem(BurnhopeDevice& device);
  ~SSGISystem();

  void computeSSGI(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const SsgiHeapPC& pc,
      uint32_t width,
      uint32_t height);

  void computeDenoise(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const SsgiDenoiseHeapPC& pc,
      uint32_t width,
      uint32_t height);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<ComputeDispatch> ssgiShader_;
  std::unique_ptr<ComputeDispatch> denoiseShader_;
};

} // namespace burnhope

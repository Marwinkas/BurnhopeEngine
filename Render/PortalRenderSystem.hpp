#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include "Core/GraphicsDispatch.hpp"
#include "Model.hpp"
#include <memory>

namespace burnhope {

class PortalRenderSystem final {
public:
  explicit PortalRenderSystem(BurnhopeDevice& device);
  ~PortalRenderSystem() = default;

  void drawDepthReset(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const PortalVertPC& basePush,
      const float4x4& model,
      uint32_t ref);

  void drawMask(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const PortalVertPC& basePush,
      const float4x4& modelMatrix,
      uint32_t refValue);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<BurnhopeModel> portalModel_;
  std::unique_ptr<GraphicsDispatch> maskShader_;
  std::unique_ptr<GraphicsDispatch> depthResetShader_;
};

} // namespace burnhope

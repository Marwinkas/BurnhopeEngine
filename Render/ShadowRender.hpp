#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Components.hpp"
#include "../Utils/Device.hpp"
#include "Core/GraphicsDispatch.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <memory>

namespace burnhope {

class ShadowRenderSystem final {
public:
  ShadowRenderSystem(BurnhopeDevice& device);
  ~ShadowRenderSystem() = default;

  ShadowRenderSystem(const ShadowRenderSystem&) = delete;
  ShadowRenderSystem& operator=(const ShadowRenderSystem&) = delete;

  void renderShadow(
      VkCommandBuffer commandBuffer,
      const BindlessRegistry& bindless,
      const ShadowVertPC& heapPush,
      const float4x4& lightSpaceMatrix,
      flecs::world& registry,
      bool renderDynamicOnly);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<GraphicsDispatch> shader_;
};

} // namespace burnhope

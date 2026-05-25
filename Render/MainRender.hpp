#pragma once

#include "../Utils/BindlessPush.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Device.hpp"
#include "../Utils/FrameInfo.hpp"
#include "Core/GraphicsDispatch.hpp"
#include "Core/SceneGpuTypes.hpp"
#include <memory>

namespace burnhope {

class GeometryRenderSystem final {
public:
  explicit GeometryRenderSystem(BurnhopeDevice& device);
  ~GeometryRenderSystem() = default;

  GeometryRenderSystem(const GeometryRenderSystem&) = delete;
  GeometryRenderSystem& operator=(const GeometryRenderSystem&) = delete;

  void renderEntities(
      FrameInfo& frameInfo,
      const BindlessRegistry& bindless,
      const GfxHeapPC& heapPush,
      uint32_t totalSubMeshCount,
      bool useStencil,
      uint32_t vrsMode = 0,
      bool isZPrepass = false);

private:
  BurnhopeDevice& device_;
  std::unique_ptr<GraphicsDispatch> shader_;
  std::unique_ptr<GraphicsDispatch> portalShader_;
  std::unique_ptr<GraphicsDispatch> zPrepassShader_;
  std::unique_ptr<GraphicsDispatch> zPrepassPortalShader_;
};

} // namespace burnhope

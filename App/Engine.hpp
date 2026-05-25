#pragma once

#include "AnimationSystem.hpp"
#include "Systems/RTReflectionSystem.hpp"
#include "Systems/VolumetricSystem.hpp"
#include "../Render/Deferred.hpp"
#include "../Render/Gbuffer.hpp"
#include "../Render/GTAOSystem.hpp"
#include "../Render/HiZSystem.hpp"
#include "../Render/MainRender.hpp"
#include "../Render/Core/SceneAddresses.hpp"
#include "../Render/RenderDebug.hpp"
#include "../Render/Material.hpp"
#include "../Render/PortalRenderSystem.hpp"
#include "../Render/RadianceCascades.hpp"
#include "../Render/SSGISystem.hpp"
#include "../Render/CullingSystem.hpp"
#include "../Render/ShadowRender.hpp"
#include "../Render/shadow.hpp"
#include "../Render/Core/ComputeDispatch.hpp"
#include "../Utils/AssetPool.hpp"
#include "../Utils/BindlessRegistry.hpp"
#include "../Utils/Components.hpp"
#include "../Utils/Device.hpp"
#include "../Utils/Renderer.hpp"
#include "../Utils/UIManager.h"
#include "../Utils/Window.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

class Engine final {
public:
  static constexpr int kWidth = 800;
  static constexpr int kHeight = 600;
  static constexpr int kMaxPortals = 10;


  Engine();
  ~Engine();
  Engine(const Engine&) = delete;
  Engine& operator=(const Engine&) = delete;

  void run();

  std::unique_ptr<BurnhopeBuffer> tlasBuffer;
  std::unique_ptr<BurnhopeBuffer> instancesBuffer;
  VkAccelerationStructureKHR tlasHandle{VK_NULL_HANDLE};

  void buildTLAS(flecs::world& world);
  [[nodiscard]] BindlessRegistry& bindless() noexcept { return bindless_; }
  [[nodiscard]] const FrameBindSlots& slots() const noexcept { return bindless_.slots(); }

private:
  friend void refreshBindlessSlots(Engine& engine);

  void loadGameObjects(flecs::world& world);
  void initCompute();
  void rebuildGBufferDescriptorSets();
  void buildPendingBlas(flecs::world& world);
  void rebuildBatches(flecs::world& world, GeometryRenderSystem& renderSystem);

  BurnhopeWindow window_{kWidth, kHeight, "BurnHope Engine"};
  BurnhopeDevice device_{window_};
  BurnhopeRenderer renderer_{window_, device_};
  BindlessRegistry bindless_;

  std::array<std::unique_ptr<BurnhopeBuffer>, BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT> frameUboBuffers_{};
  std::array<uint32_t, BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT> frameGlobalUboHeap_{};

  std::unique_ptr<DeferredLightingSystem> lightingSystem;
  std::unique_ptr<BurnhopeTexture> hdrOutputTexture;
  std::unique_ptr<BurnhopeTexture> gtaoOutputTexture;
  std::unique_ptr<ShadowRenderSystem> shadowRenderSystem;
  uint32_t totalSubMeshCount{0};
  uint32_t maxMaterialIndex{0};
  std::unique_ptr<BurnhopeBuffer> dummyGridBuffer;
  std::unique_ptr<BurnhopeBuffer> dummyIndexBuffer;
  std::unique_ptr<BurnhopeTexture> ssgiRawTexture;
  std::unique_ptr<BurnhopeTexture> taaHistoryTexture;
  std::unique_ptr<BurnhopeTexture> taaResolvedTexture;
  std::unique_ptr<BurnhopeTexture> postProcessTexture;
  std::unique_ptr<ComputeDispatch> postProcessShader;
  std::unique_ptr<BurnhopeBuffer> exposureBuffer;
  std::unique_ptr<AnimationSystem> animationSystem;
  std::unique_ptr<BurnhopeBuffer> boneMatricesBuffer;
  std::unique_ptr<ComputeDispatch> lightCullingShader;
  std::unique_ptr<ComputeDispatch> vsmMarkPagesShader;
  std::unique_ptr<SSGISystem> ssgiSystem;
  std::unique_ptr<CullingSystem> cullingSystem;
  std::unique_ptr<GeometryRenderSystem> geometryRenderSystem;
  std::unique_ptr<PortalRenderSystem> portalRenderSystem;
  std::unique_ptr<RadianceCascadesSystem> rcSystem;
  TextureHandle defaultWhiteTex{kInvalidTextureHandle};
  TextureHandle defaultNormalTex{kInvalidTextureHandle};
  std::shared_ptr<Material> defaultWhiteMaterial;
  TextureHandle defaultDirtTex{kInvalidTextureHandle};
  TextureHandle blueNoiseTex{kInvalidTextureHandle};
  std::unique_ptr<BurnhopeShadowSystem> shadowSystem;
  std::unique_ptr<BurnhopeBuffer> lightUboBuffer;
  std::unique_ptr<HiZSystem> hizSystem;
  std::unique_ptr<GTAOSystem> gtaoSystem;
  std::vector<std::vector<std::unique_ptr<BurnhopeBuffer>>> portalUboBuffers;
  std::array<std::array<uint32_t, BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT>, kMaxPortals> portalFrameUboHeap_{};
  flecs::world world;
  std::unique_ptr<UIManager> uiManager;
  std::unique_ptr<BurnhopeBuffer> objectBuffer;
  std::unique_ptr<BurnhopeBuffer> materialBuffer;
  std::unique_ptr<BurnhopeBuffer> sceneAddressesBuffer;
  std::unique_ptr<BurnhopeBuffer> faceMatricesBuffer;
  std::unique_ptr<BurnhopeBuffer> portalUbosBuffer;
  VkImageView csmArrayView{VK_NULL_HANDLE};
  std::unique_ptr<BurnhopeGBuffer> gBuffer;
  std::unique_ptr<RTReflectionSystem> rtReflectionSystem;
  std::unique_ptr<VolumetricSystem> volumetricSystem;
  std::unique_ptr<BurnhopeBuffer> decalBuffer;
  uint32_t rtTlasHeap_{0};
};

using FirstApp = Engine;

} // namespace burnhope

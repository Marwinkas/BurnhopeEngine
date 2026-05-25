#include "Engine.hpp"
#include "BindlessSetup.hpp"
#include "../Render/Camera.hpp"
#include <iostream>

namespace burnhope {

Engine::Engine() : bindless_{device_} {
  auto white = std::make_unique<BurnhopeTexture>(device_, "../textures/diffuse3.png");
  defaultWhiteTex = bindless_.textures().emplace(std::move(white), bindless_);
  defaultNormalTex = defaultWhiteTex;
  shadowSystem = std::make_unique<BurnhopeShadowSystem>(device_);
  defaultWhiteMaterial = std::make_shared<Material>();
  defaultWhiteMaterial->repeatTexture = true;

  animationSystem = std::make_unique<AnimationSystem>();
  boneMatricesBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(float4x4), 100000,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  boneMatricesBuffer->map();

  if (std::filesystem::exists("../textures/lens_dirt.png")) {
    auto dirt = std::make_unique<BurnhopeTexture>(device_, "../textures/lens_dirt.png");
    defaultDirtTex = bindless_.textures().emplace(std::move(dirt), bindless_);
  } else {
    defaultDirtTex = defaultWhiteTex;
  }

  for (int i = 0; i < BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
    frameUboBuffers_[i] = std::make_unique<BurnhopeBuffer>(
        device_, sizeof(GlobalUbo), 1,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    frameUboBuffers_[i]->map();
  }

  portalUboBuffers.resize(kMaxPortals);
  for (int p = 0; p < kMaxPortals; ++p) {
    portalUboBuffers[p].resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int f = 0; f < BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT; ++f) {
      portalUboBuffers[p][f] = std::make_unique<BurnhopeBuffer>(
          device_, sizeof(GlobalUbo), 1,
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      portalUboBuffers[p][f]->map();
    }
  }

  sceneAddressesBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(SceneAddresses), 1,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  sceneAddressesBuffer->map();

  gBuffer = std::make_unique<BurnhopeGBuffer>(device_, window_.getExtent());
  geometryRenderSystem = std::make_unique<GeometryRenderSystem>(device_);
  portalRenderSystem = std::make_unique<PortalRenderSystem>(device_);
  uiManager = std::make_unique<UIManager>(
      window_, device_, renderer_.getSwapChainImageFormat(), &world,
      std::filesystem::current_path().string());
  {
    auto& ctx = uiManager->GetContext();
    ctx.texturePool = &bindless_.textures();
    ctx.bindless = &bindless_;
  }

  initCompute();
  loadGameObjects(world);
}

Engine::~Engine() {
  vkDeviceWaitIdle(device_.device());
  if (tlasHandle != VK_NULL_HANDLE) {
    auto pfn = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        vkGetDeviceProcAddr(device_.device(), "vkDestroyAccelerationStructureKHR"));
    if (pfn) {
      pfn(device_.device(), tlasHandle, nullptr);
    }
  }
  uiManager.reset();
  if (csmArrayView != VK_NULL_HANDLE) {
    vkDestroyImageView(device_.device(), csmArrayView, nullptr);
  }
}

} // namespace burnhope

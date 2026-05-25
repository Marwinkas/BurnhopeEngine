#include "BindlessSetup.hpp"
#include "Engine.hpp"
#include "Systems/GpuProbeTypes.hpp"
#include "../Utils/BindlessPush.hpp"
#include "../Render/Core/SceneGpuTypes.hpp"
#include "../Render/Core/GlobalUboLayout.hpp"
#include "../Render/Core/MaterialGpuLayout.hpp"
#include "../Render/RenderDebug.hpp"
#include "../Render/CullingSystem.hpp"
#include "../Render/RadianceCascades.hpp"
#include "../Utils/SwapChain.hpp"
#include <iostream>

namespace burnhope {

void refreshBindlessSlots(Engine& e) {
  vkDeviceWaitIdle(e.device_.device());
  static bool strideLogOnce = false;
  if (!strideLogOnce) {
    const VkDeviceSize heapStride = e.device_.resourceHeapDescriptorStride();
    const VkDeviceSize img = e.device_.descriptorSize(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    const VkDeviceSize ssbo = e.device_.descriptorSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    const VkDeviceSize resReserved = e.device_.caps().heapProps.minResourceHeapReservedRange;
    const VkDeviceSize samReserved = e.device_.caps().heapProps.minSamplerHeapReservedRange;
    std::cerr << "[Bindless] heapSlotStride=" << heapStride << " sampledImage=" << img
              << " storageBuffer=" << ssbo
              << " resourceReserved=" << resReserved << " samplerReserved=" << samReserved
              << " maxPushDataSize=" << e.device_.caps().maxPushDataSize
              << " gfxPushBytes=" << kGfxHeapPushBytes
              << " heapResourceBase@" << kGfxWireHeapResourceBase
              << " objectDataBytes=" << sizeof(ObjectData) << '\n';
    strideLogOnce = true;
  }
  e.bindless_.resetResourceHeap();
  e.bindless_.textures().refreshHeapIndices(e.bindless_);
  auto& s = e.bindless_.slots();

  for (int i = 0; i < BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT; ++i) {
    if (e.frameUboBuffers_[i]) {
      s.globalUbo[i] = e.bindless_.registerUniformBuffer(
          e.frameUboBuffers_[i]->handle(), 0, sizeof(GlobalUbo));
      e.frameGlobalUboHeap_[i] = s.globalUbo[i];
    }
  }
  if (e.objectBuffer) {
    s.objectStorage = e.bindless_.registerStorageBuffer(
        e.objectBuffer->handle(), 0, e.objectBuffer->bufferSize());
  }
  if (e.materialBuffer) {
    s.materialStorage = e.bindless_.registerStorageBuffer(
        e.materialBuffer->handle(), 0, e.materialBuffer->bufferSize());
  }
  if (e.boneMatricesBuffer) {
    s.boneStorage = e.bindless_.registerStorageBuffer(
        e.boneMatricesBuffer->handle(), 0, e.boneMatricesBuffer->bufferSize());
  }
  if (e.lightUboBuffer) {
    s.lightBuffer = e.bindless_.registerStorageBuffer(
        e.lightUboBuffer->handle(), 0, e.lightUboBuffer->bufferSize());
  }
  if (e.decalBuffer) {
    s.decalBuffer = e.bindless_.registerStorageBuffer(
        e.decalBuffer->handle(), 0, e.decalBuffer->bufferSize());
  }
  if (e.dummyGridBuffer) {
    s.lightGrid = e.bindless_.registerStorageBuffer(
        e.dummyGridBuffer->handle(), 0, e.dummyGridBuffer->bufferSize());
  }
  if (e.dummyIndexBuffer) {
    s.lightIndexList = e.bindless_.registerStorageBuffer(
        e.dummyIndexBuffer->handle(), 0, e.dummyIndexBuffer->bufferSize());
  }
  if (e.faceMatricesBuffer) {
    s.faceMatrices = e.bindless_.registerStorageBuffer(
        e.faceMatricesBuffer->handle(), 0, e.faceMatricesBuffer->bufferSize());
  }
  if (e.portalUbosBuffer) {
    s.portalUbos = e.bindless_.registerStorageBuffer(
        e.portalUbosBuffer->handle(), 0, e.portalUbosBuffer->bufferSize());
  }
  for (int p = 0; p < Engine::kMaxPortals; ++p) {
    for (int f = 0; f < BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT; ++f) {
      if (p < static_cast<int>(e.portalUboBuffers.size()) &&
          f < static_cast<int>(e.portalUboBuffers[p].size()) && e.portalUboBuffers[p][f]) {
        e.portalFrameUboHeap_[static_cast<std::size_t>(p)][static_cast<std::size_t>(f)] =
            e.bindless_.registerUniformBuffer(
                e.portalUboBuffers[p][f]->handle(), 0, sizeof(GlobalUbo));
      }
    }
  }
  if (e.blueNoiseTex != kInvalidTextureHandle) {
    auto* tex = e.bindless_.textures().resolve(e.blueNoiseTex);
    if (tex) {
      s.blueNoise = e.bindless_.registerSampledImage(*tex);
    }
  }
  if (e.shadowSystem && e.shadowSystem->getAtlas()) {
    s.shadowAtlas = e.bindless_.registerSampledImage(
        *e.shadowSystem->getAtlas()->getTexture());
  }
  if (e.shadowSystem && e.shadowSystem->getCSM()) {
    s.shadowCsm = e.bindless_.registerSampledImage(
        *e.shadowSystem->getCSM()->getTexture(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  if (e.shadowSystem && e.shadowSystem->getVSM()) {
    auto* vsm = e.shadowSystem->getVSM();
    s.vsmAtlas = e.bindless_.registerSampledImage(
        *vsm->getPhysicalAtlas(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    s.vsmPageTable = e.bindless_.registerStorageBuffer(
        vsm->getPageTable()->handle(), 0, vsm->getPageTable()->bufferSize());
    s.vsmAllocator = e.bindless_.registerStorageBuffer(
        vsm->getAllocator()->handle(), 0, vsm->getAllocator()->bufferSize());
  }
  if (e.exposureBuffer) {
    s.exposureBuffer = e.bindless_.registerStorageBuffer(
        e.exposureBuffer->handle(), 0, e.exposureBuffer->bufferSize());
  }

  if (e.gBuffer) {
    s.gbufferNormal = e.bindless_.registerSampledImage(*e.gBuffer->getNormalRoughness());
    s.gbufferAlbedo = e.bindless_.registerSampledImage(*e.gBuffer->getAlbedoMetallic());
    s.gbufferHeightAo = e.bindless_.registerSampledImage(*e.gBuffer->getHeightAO());
    s.gbufferEmissive = e.bindless_.registerSampledImage(*e.gBuffer->getEmissive());
    s.gbufferPortalId = e.bindless_.registerSampledImage(*e.gBuffer->getPortalID());
    s.gbufferDepth = e.bindless_.registerSampledImage(*e.gBuffer->getDepth());
  }
  if (e.hdrOutputTexture) {
    s.hdrOutputSampled = e.bindless_.registerSampledImage(*e.hdrOutputTexture, VK_IMAGE_LAYOUT_GENERAL);
    s.hdrOutput = e.bindless_.registerStorageImage(*e.hdrOutputTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.gtaoOutputTexture) {
    s.gtaoOutput = e.bindless_.registerStorageImage(*e.gtaoOutputTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.ssgiRawTexture) {
    s.ssgiRaw = e.bindless_.registerStorageImage(*e.ssgiRawTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.postProcessTexture) {
    s.postProcess = e.bindless_.registerSampledImage(*e.postProcessTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.taaHistoryTexture) {
    s.taaHistory = e.bindless_.registerSampledImage(*e.taaHistoryTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.taaResolvedTexture) {
    s.taaResolved = e.bindless_.registerSampledImage(*e.taaResolvedTexture, VK_IMAGE_LAYOUT_GENERAL);
  }
  if (e.rtReflectionSystem && e.rtReflectionSystem->rtReflectionsTexture) {
    e.rtReflectionSystem->rtHeapIndex = e.bindless_.registerStorageImage(
        *e.rtReflectionSystem->rtReflectionsTexture, VK_IMAGE_LAYOUT_GENERAL);
    s.rtReflections = e.rtReflectionSystem->rtHeapIndex;
  }
  if (e.volumetricSystem && e.volumetricSystem->volumetricTex) {
    e.volumetricSystem->volumetricHeapIndex = e.bindless_.registerStorageImage(
        *e.volumetricSystem->volumetricTex, VK_IMAGE_LAYOUT_GENERAL);
    e.volumetricSystem->volumetricSampledHeapIndex = e.bindless_.registerSampledImage(
        *e.volumetricSystem->volumetricTex, VK_IMAGE_LAYOUT_GENERAL);
    s.volumetric = e.volumetricSystem->volumetricSampledHeapIndex;
    s.volumetricStorage = e.volumetricSystem->volumetricHeapIndex;
  }
  if (e.rcSystem) {
    s.giDiffuse = e.rcSystem->refreshGiHeapSlot(e.bindless_);
    s.giSpecular = s.giDiffuse;
    s.giCascade0 = s.giDiffuse;
  }
  if (e.hizSystem) {
    s.hiZ = e.hizSystem->hiZHeapIndex();
  }
  if (e.tlasHandle != VK_NULL_HANDLE) {
    e.rtTlasHeap_ = e.bindless_.registerAccelerationStructure(e.tlasHandle);
    s.rtTlas = e.rtTlasHeap_;
  }

  s.textureTableBase = e.bindless_.textures().heapIndex(e.defaultWhiteTex);
  s.textureTableCount = e.bindless_.textures().count();
  if constexpr (kMinimalRenderPath) {
    static bool texHeapLogOnce = false;
    if (!texHeapLogOnce) {
      const uint32_t whiteSlot = s.textureTableBase;
      const uint32_t imgBinding =
          heapBindingIndex(whiteSlot, e.device_, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
      std::cerr << "[Bindless] whiteTex heapSlot=" << whiteSlot
                << " sampledImageBinding=" << imgBinding
                << " defaultSamplerSlot=" << s.defaultSampler << '\n';
      texHeapLogOnce = true;
    }
  }
}

void Engine::initCompute() {
  VkExtent3D extent = {window_.getExtent().width, window_.getExtent().height, 1};

  lightUboBuffer = std::make_unique<BurnhopeBuffer>(
      device_, 65536, 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  lightUboBuffer->map();

  faceMatricesBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(PointFaceMatrices), 100, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  faceMatricesBuffer->map();

  static constexpr uint32_t kClusterCount = 16 * 9 * 24;
  dummyGridBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(uint32_t) * 2, kClusterCount,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  dummyIndexBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(uint32_t), 65536,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  portalUbosBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(GlobalUbo) * kMaxPortals, 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  portalUbosBuffer->map();

  hdrOutputTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_SAMPLE_COUNT_1_BIT);
  gtaoOutputTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R8_UNORM, extent, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SAMPLE_COUNT_1_BIT);
  ssgiRawTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
  postProcessTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
      VK_SAMPLE_COUNT_1_BIT);
  taaHistoryTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
      VK_SAMPLE_COUNT_1_BIT);
  taaResolvedTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, extent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      VK_SAMPLE_COUNT_1_BIT);

  exposureBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(float), 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  exposureBuffer->map();
  float initLuma = 1.0f;
  exposureBuffer->writeToBuffer(&initLuma);

  decalBuffer = std::make_unique<BurnhopeBuffer>(
      device_, sizeof(DecalBlock), 1, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  decalBuffer->map();

  lightingSystem = std::make_unique<DeferredLightingSystem>(device_);
  gtaoSystem = std::make_unique<GTAOSystem>(device_);
  ssgiSystem = std::make_unique<SSGISystem>(device_);
  postProcessShader = std::make_unique<ComputeDispatch>(
      device_, "shaders/post_process.comp.spv", sizeof(PostProcessHeapPC));
  lightCullingShader = std::make_unique<ComputeDispatch>(
      device_, "shaders/light_culling.comp.spv", sizeof(LightCullHeapPC));
  vsmMarkPagesShader = std::make_unique<ComputeDispatch>(
      device_, "shaders/vsm_mark_pages.comp.spv", sizeof(VsmMarkHeapPC));

  shadowRenderSystem = std::make_unique<ShadowRenderSystem>(device_);
  hizSystem = std::make_unique<HiZSystem>(device_, bindless_, 100000);
  rtReflectionSystem = std::make_unique<RTReflectionSystem>(device_, bindless_);
  volumetricSystem = std::make_unique<VolumetricSystem>(device_, bindless_);
  rcSystem = std::make_unique<RadianceCascadesSystem>(device_, bindless_, window_.getExtent());
  cullingSystem = std::make_unique<CullingSystem>(device_, bindless_, 100000);

  rtReflectionSystem->init(window_.getExtent());
  volumetricSystem->init(window_.getExtent());

  const auto transitionStorage = [&](BurnhopeTexture& tex) {
    device_.transitionImageLayout(
        tex.getImage(), tex.getFormat(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  };
  transitionStorage(*hdrOutputTexture);
  transitionStorage(*gtaoOutputTexture);
  transitionStorage(*ssgiRawTexture);
  transitionStorage(*postProcessTexture);
  transitionStorage(*taaHistoryTexture);
  transitionStorage(*taaResolvedTexture);

  refreshBindlessSlots(*this);
}

} // namespace burnhope

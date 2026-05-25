#include "RadianceCascades.hpp"
#include "Texture.hpp"

namespace burnhope {

RadianceCascadesSystem::RadianceCascadesSystem(
    BurnhopeDevice& device,
    BindlessRegistry& bindless,
    VkExtent2D extent)
    : device_{device}, bindless_{bindless}, extent_{extent} {
  mergeShader_ =
      std::make_unique<ComputeDispatch>(device_, "shaders/cascade_merge.comp.spv", sizeof(CascadeMergePC));
  sampleShader_ =
      std::make_unique<ComputeDispatch>(device_, "shaders/gi_sample.comp.spv", sizeof(GiSampleHeapPC));
  rebuildOnResize(extent);
}

RadianceCascadesSystem::~RadianceCascadesSystem() = default;

bool RadianceCascadesSystem::needsRebuild(int px, int py, int pz, int octa, float baseLen) const {
  return probeX_ != px || probeY_ != py || probeZ_ != pz || octaSize_ != octa ||
         baseRayLength_ != baseLen;
}

void RadianceCascadesSystem::updateConfig(int px, int py, int pz, int octa, float baseLen) {
  probeX_ = px;
  probeY_ = py;
  probeZ_ = pz;
  octaSize_ = octa;
  baseRayLength_ = baseLen;
}

void RadianceCascadesSystem::rebuildOnResize(VkExtent2D extent) {
  extent_ = extent;
  const VkExtent3D giExtent{extent.width, extent.height, 1};
  giDiffuseTex_ = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, giExtent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
  giDiffuseHeap_ = refreshGiHeapSlot(bindless_);
  bindless_.slots().giDiffuse = giDiffuseHeap_;
  bindless_.slots().giCascade0 = giDiffuseHeap_;
}

uint32_t RadianceCascadesSystem::refreshGiHeapSlot(BindlessRegistry& bindless) {
  if (!giDiffuseTex_) {
    return 0;
  }
  giDiffuseHeap_ = bindless.registerSampledImage(*giDiffuseTex_, VK_IMAGE_LAYOUT_GENERAL);
  return giDiffuseHeap_;
}

void RadianceCascadesSystem::dispatch(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const CascadeMergePC& mergePush,
    const GiSampleHeapPC& samplePush,
    VkExtent2D screenExtent) {
  mergeShader_->bind(cmd);
  mergeShader_->pushConstants(cmd, bindless, &mergePush, sizeof(mergePush));
  mergeShader_->dispatch(cmd, 1, 1, 1);

  sampleShader_->bind(cmd);
  sampleShader_->pushConstants(cmd, bindless, &samplePush, sizeof(samplePush));
  sampleShader_->dispatch(
      cmd, (screenExtent.width + 15) / 16, (screenExtent.height + 15) / 16, 1);
}

} // namespace burnhope

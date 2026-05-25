#include "VolumetricSystem.hpp"
#include "../../Utils/BindlessPush.hpp"

namespace burnhope {

VolumetricSystem::VolumetricSystem(BurnhopeDevice& device, BindlessRegistry& bindless)
    : device_{device}, bindless_{bindless} {
  shader = std::make_unique<ComputeDispatch>(
      device_, "shaders/volumetric.comp.spv", sizeof(VolumetricHeapPC));
}

void VolumetricSystem::init(VkExtent2D extent) {
  const VkExtent3D volExtent = {std::max(1u, extent.width / 2), std::max(1u, extent.height / 2), 1};
  volumetricTex = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, volExtent,
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
  volumetricHeapIndex = bindless_.registerStorageImage(*volumetricTex, VK_IMAGE_LAYOUT_GENERAL);
  volumetricSampledHeapIndex = bindless_.registerSampledImage(*volumetricTex, VK_IMAGE_LAYOUT_GENERAL);
}

void VolumetricSystem::updateDescriptors(VkExtent2D extent) { init(extent); }

} // namespace burnhope

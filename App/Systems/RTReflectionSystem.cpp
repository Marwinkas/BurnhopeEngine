#include "RTReflectionSystem.hpp"
#include "../../Utils/BindlessPush.hpp"

namespace burnhope {

RTReflectionSystem::RTReflectionSystem(BurnhopeDevice& device, BindlessRegistry& bindless)
    : device_{device}, bindless_{bindless} {
  rtReflectionsShader = std::make_unique<ComputeDispatch>(
      device_, "shaders/rt_reflections.comp.spv", sizeof(RtReflectionHeapPC));
  probeRenderShader = std::make_unique<ComputeDispatch>(
      device_, "shaders/probe_render.comp.spv", sizeof(ProbeRenderHeapPC));
}

void RTReflectionSystem::init(VkExtent2D extent) {
  rtReflectionsTexture = std::make_unique<BurnhopeTexture>(
      device_, VK_FORMAT_R16G16B16A16_SFLOAT, VkExtent3D{extent.width, extent.height, 1},
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_SAMPLE_COUNT_1_BIT);
  rtHeapIndex = bindless_.registerStorageImage(*rtReflectionsTexture, VK_IMAGE_LAYOUT_GENERAL);
}

void RTReflectionSystem::updateDescriptors(VkExtent2D extent, TextureHandle) {
  init(extent);
}

} // namespace burnhope

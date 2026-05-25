#include "HiZSystem.hpp"

namespace burnhope {

HiZSystem::HiZSystem(BurnhopeDevice& device, BindlessRegistry& bindless, uint32_t)
    : device_{device}, bindless_{bindless} {
  shader_ = std::make_unique<ComputeDispatch>(
      device_, "shaders/hiz_downsample.comp.spv", sizeof(HiZHeapPC));
}

HiZSystem::~HiZSystem() {
  if (fullView_) {
    vkDestroyImageView(device_.device(), fullView_, nullptr);
  }
  if (hiZSampler_) {
    vkDestroySampler(device_.device(), hiZSampler_, nullptr);
  }
  if (hiZImage_) {
    vmaDestroyImage(device_.allocator(), hiZImage_, hiZMemory_);
  }
}

void HiZSystem::build(VkExtent2D extent, VkImageView, VkSampler) {
  (void)extent;
  hiZHeapIndex_ = 0;
}

void HiZSystem::dispatch(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    uint32_t depthInHeapIndex,
    uint32_t hiZOutHeapIndex,
    VkExtent2D extent) {
  HiZHeapPC pc{};
  pc.depthIn = depthInHeapIndex;
  pc.hiZOut = hiZOutHeapIndex;
  pc.invSize = {1.0f / static_cast<float>(std::max(1u, extent.width)),
                1.0f / static_cast<float>(std::max(1u, extent.height))};
  pc.defaultSampler = bindless.slots().defaultSampler;
  hiZHeapIndex_ = hiZOutHeapIndex;

  shader_->bind(cmd);
  shader_->pushConstants(cmd, bindless, &pc, sizeof(pc));
  shader_->dispatch(cmd, (extent.width + 15) / 16, (extent.height + 15) / 16, 1);
}

} // namespace burnhope

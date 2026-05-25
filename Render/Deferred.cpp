#include "Deferred.hpp"

namespace burnhope {

DeferredLightingSystem::DeferredLightingSystem(BurnhopeDevice& device) : device_{device} {
  shader_ = std::make_unique<ComputeDispatch>(
      device_, "shaders/lighting.comp.spv", sizeof(LightingHeapPC));
}

DeferredLightingSystem::~DeferredLightingSystem() = default;

void DeferredLightingSystem::computeLighting(
    VkCommandBuffer commandBuffer,
    const BindlessRegistry& bindless,
    const LightingHeapPC& pc,
    uint32_t width,
    uint32_t height) {
  shader_->bind(commandBuffer);
  shader_->pushConstants(commandBuffer, bindless, &pc, sizeof(pc));
  shader_->dispatch(commandBuffer, (width + 15) / 16, (height + 15) / 16);
}

} // namespace burnhope

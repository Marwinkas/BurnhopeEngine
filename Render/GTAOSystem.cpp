#include "GTAOSystem.hpp"

namespace burnhope {

GTAOSystem::GTAOSystem(BurnhopeDevice& device) : device_{device} {
  shader_ = std::make_unique<ComputeDispatch>(device_, "shaders/gtao.comp.spv", sizeof(GtaoHeapPC));
}

GTAOSystem::~GTAOSystem() = default;

void GTAOSystem::compute(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const GtaoHeapPC& pc,
    uint32_t width,
    uint32_t height) {
  shader_->bind(cmd);
  shader_->pushConstants(cmd, bindless, &pc, sizeof(pc));
  shader_->dispatch(cmd, (width + 15) / 16, (height + 15) / 16);
}

} // namespace burnhope

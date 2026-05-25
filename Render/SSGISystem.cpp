#include "SSGISystem.hpp"

namespace burnhope {

SSGISystem::SSGISystem(BurnhopeDevice& device) : device_{device} {
  ssgiShader_ = std::make_unique<ComputeDispatch>(device_, "shaders/ssgi.comp.spv", sizeof(SsgiHeapPC));
  denoiseShader_ =
      std::make_unique<ComputeDispatch>(device_, "shaders/ssgi_denoise.comp.spv", sizeof(SsgiDenoiseHeapPC));
}

SSGISystem::~SSGISystem() = default;

void SSGISystem::computeSSGI(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const SsgiHeapPC& pc,
    uint32_t width,
    uint32_t height) {
  ssgiShader_->bind(cmd);
  ssgiShader_->pushConstants(cmd, bindless, &pc, sizeof(pc));
  ssgiShader_->dispatch(cmd, (width + 7) / 8, (height + 7) / 8);
}

void SSGISystem::computeDenoise(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const SsgiDenoiseHeapPC& pc,
    uint32_t width,
    uint32_t height) {
  denoiseShader_->bind(cmd);
  denoiseShader_->pushConstants(cmd, bindless, &pc, sizeof(pc));
  denoiseShader_->dispatch(cmd, (width + 7) / 8, (height + 7) / 8);
}

} // namespace burnhope

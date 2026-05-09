#include "SSGISystem.hpp"

namespace burnhope {

SSGISystem::SSGISystem(
    BurnhopeDevice& device,
    VkDescriptorSetLayout globalSetLayout,
    VkDescriptorSetLayout gBufferLayout,
    VkDescriptorSetLayout shadowLayout,
    VkDescriptorSetLayout ssgiLayout)
    : lveDevice{device} {
    
    std::vector<VkDescriptorSetLayout> ssgiLayouts = { globalSetLayout, gBufferLayout, shadowLayout, ssgiLayout };
    ssgiShader = std::make_unique<ComputeShader>(lveDevice, "shaders/ssgi.comp.spv", ssgiLayouts);

    std::vector<VkDescriptorSetLayout> denoiseLayouts = { globalSetLayout, gBufferLayout, ssgiLayout };
    denoiseShader = std::make_unique<ComputeShader>(lveDevice, "shaders/ssgi_denoise.comp.spv", denoiseLayouts);
}

SSGISystem::~SSGISystem() = default;

void SSGISystem::computeSSGI(
    VkCommandBuffer cmd,
    VkDescriptorSet globalSet,
    VkDescriptorSet gBufferSet,
    VkDescriptorSet shadowSet,
    VkDescriptorSet ssgiSet,
    uint32_t width,
    uint32_t height) {
    
    ssgiShader->bind(cmd);
    ssgiShader->bindDescriptorSets(cmd, {globalSet, gBufferSet, shadowSet, ssgiSet});
    
    ssgiShader->dispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
}

void SSGISystem::computeDenoise(
    VkCommandBuffer cmd,
    VkDescriptorSet globalSet,
    VkDescriptorSet gBufferSet,
    VkDescriptorSet ssgiSet,
    uint32_t width,
    uint32_t height) {
    
    denoiseShader->bind(cmd);
    denoiseShader->bindDescriptorSets(cmd, {globalSet, gBufferSet, ssgiSet});
    denoiseShader->dispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
}

} // namespace burnhope
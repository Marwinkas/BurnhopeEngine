#include "GTAOSystem.hpp"
namespace burnhope
{
    GTAOSystem::GTAOSystem(BurnhopeDevice &device, const std::vector<VkDescriptorSetLayout> &layouts)
        : lveDevice(device)
    {
        shader = std::make_unique<ComputeShader>(device, "shaders/gtao.comp.spv", layouts);
    }
    GTAOSystem::~GTAOSystem() = default;
    void GTAOSystem::compute(VkCommandBuffer cmd, VkDescriptorSet globalSet, VkDescriptorSet gtaoSet, uint32_t width, uint32_t height)
    {
        shader->bind(cmd);
        shader->bindDescriptorSets(cmd, {globalSet, gtaoSet});
        shader->dispatch(cmd, (width + 15) / 16, (height + 15) / 16);
    }
}
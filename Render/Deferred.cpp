#include "Deferred.hpp"
#include "ComputeShader.hpp"
#include <stdexcept>
#include <cassert>
namespace burnhope
{
    DeferredLightingSystem::DeferredLightingSystem(
        BurnhopeDevice &device, const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts)
        : lveDevice{device}
    {
        shader = std::make_unique<ComputeShader>(
            device, "shaders/lighting.comp.spv", descriptorSetLayouts);
    }
    DeferredLightingSystem::~DeferredLightingSystem() = default;
    void DeferredLightingSystem::computeLighting(
        VkCommandBuffer commandBuffer,
        const std::vector<VkDescriptorSet> &descriptorSets,
        uint32_t width, uint32_t height)
    {
        shader->bind(commandBuffer);
        shader->bindDescriptorSets(commandBuffer, descriptorSets);
        uint32_t groupCountX = (width + 15) / 16;
        uint32_t groupCountY = (height + 15) / 16;
        shader->dispatch(commandBuffer, groupCountX, groupCountY, 1);
    }
}
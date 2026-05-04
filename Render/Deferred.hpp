#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/FrameInfo.hpp"
#include <memory>
#include <vector>
#include "ComputeShader.hpp"
namespace burnhope
{
    class DeferredLightingSystem
    {
    public:
        DeferredLightingSystem(BurnhopeDevice &device, const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
        ~DeferredLightingSystem();
        DeferredLightingSystem(const DeferredLightingSystem &) = delete;
        DeferredLightingSystem &operator=(const DeferredLightingSystem &) = delete;
        void computeLighting(VkCommandBuffer commandBuffer, const std::vector<VkDescriptorSet> &descriptorSets, uint32_t width, uint32_t height);

    private:
        void createPipelineLayout(const std::vector<VkDescriptorSetLayout> &descriptorSetLayouts);
        void createPipeline();
        BurnhopeDevice &lveDevice;
        std::unique_ptr<BurnhopePipeline> lvePipeline;
        VkPipelineLayout pipelineLayout;
        std::unique_ptr<ComputeShader> shader;
    };
}
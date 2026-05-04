#include "Deferred.hpp"


#include <stdexcept>
#include <cassert>

namespace burnhope {

    DeferredLightingSystem::DeferredLightingSystem(
        BurnhopeDevice& device, const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts)
        : lveDevice{ device } {
        createPipelineLayout(descriptorSetLayouts);
        createPipeline();
    }

    DeferredLightingSystem::~DeferredLightingSystem() {
        vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
    }

    void DeferredLightingSystem::createPipelineLayout(const std::vector<VkDescriptorSetLayout>& descriptorSetLayouts) {
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
        pipelineLayoutInfo.pushConstantRangeCount = 0; 
        pipelineLayoutInfo.pPushConstantRanges = nullptr;

        if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create compute pipeline layout!");
        }
    }

    void DeferredLightingSystem::createPipeline() {
        assert(pipelineLayout != nullptr && "Cannot create compute pipeline before pipeline layout");

        
        lvePipeline = std::make_unique<BurnhopePipeline>(
            lveDevice,
            "shaders/lighting.comp.spv",
            pipelineLayout);
    }

    void DeferredLightingSystem::computeLighting(
        VkCommandBuffer commandBuffer, 
        const std::vector<VkDescriptorSet>& descriptorSets, 
        uint32_t width, uint32_t height) {

        
        lvePipeline->bindCompute(commandBuffer);

        
        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE,
            pipelineLayout,
            0,
            static_cast<uint32_t>(descriptorSets.size()),
            descriptorSets.data(),
            0,
            nullptr);

        
        
        uint32_t groupCountX = (width + 15) / 16;
        uint32_t groupCountY = (height + 15) / 16;

        
        vkCmdDispatch(commandBuffer, groupCountX, groupCountY, 1);
    }

} 
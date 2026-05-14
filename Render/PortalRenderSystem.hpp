#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Components.hpp"
#include "Model.hpp"
#include <memory>

namespace burnhope {
    class PortalRenderSystem {
    public:
        PortalRenderSystem(BurnhopeDevice& device, VkDescriptorSetLayout globalSetLayout);
        ~PortalRenderSystem();

        void drawDepthReset(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4& model, uint32_t ref);

        void drawMask(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4& modelMatrix, uint32_t refValue);

    private:
        BurnhopeDevice& lveDevice;
        VkPipelineLayout pipelineLayout;
        std::unique_ptr<BurnhopePipeline> lvePipeline;
        std::unique_ptr<BurnhopeModel> portalModel;
        std::unique_ptr<BurnhopePipeline> depthResetPipeline;

        void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
        void createPipeline(VkRenderPass renderPass);
        void createDepthResetPipeline(VkRenderPass renderPass);
    };
}
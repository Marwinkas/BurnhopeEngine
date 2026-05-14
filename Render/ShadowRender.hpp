#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/Components.hpp"
#include "../Utils/Buffer.hpp"
#include "GraphicsShader.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <memory>
namespace burnhope
{
    struct ShadowPushConstant
    {
        glm::mat4 lightSpaceMatrix;
    };
    class ShadowRenderSystem
    {
    public:
        ShadowRenderSystem(BurnhopeDevice &device,
                           VkDescriptorSetLayout globalSetLayout);
        ~ShadowRenderSystem();
        ShadowRenderSystem(const ShadowRenderSystem &) = delete;
        ShadowRenderSystem &operator=(const ShadowRenderSystem &) = delete;
        void renderShadow(
            VkCommandBuffer commandBuffer,
            const glm::mat4 &lightSpaceMatrix,
            entt::registry &registry,
             VkDescriptorSet objectStorageSet,
             bool renderDynamicOnly);

    private:
        void createPipelineLayout(VkDescriptorSetLayout objectSetLayout);
        void createPipeline(VkRenderPass renderPass);
        BurnhopeDevice &lveDevice;
        std::unique_ptr<BurnhopePipeline> pipeline;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        std::unique_ptr<GraphicsShader> shader;
    };
}

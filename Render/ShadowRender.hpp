#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/Components.hpp"
#include "../Utils/Buffer.hpp"
#include "GraphicsShader.hpp"
#include "../Utils/DirectXMathCompat.hpp"
#include <memory>
namespace burnhope
{
    struct ShadowPushConstant
    {
        float4x4 lightSpaceMatrix;
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
            const float4x4 &lightSpaceMatrix,
            flecs::world &registry,
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

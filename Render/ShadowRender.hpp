#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include "../Utils/Descriptors.hpp"
#include "../Utils/Components.hpp"
#include "../Utils/Buffer.hpp"
#include "CullingSystem.hpp"
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <memory>

namespace burnhope {

// Push constant для shadow pass — только матрица
struct ShadowPushConstant {
    glm::mat4 lightSpaceMatrix;
};

class ShadowRenderSystem {
public:
    ShadowRenderSystem(BurnhopeDevice& device,
        VkRenderPass shadowRenderPass,
        VkDescriptorSetLayout globalSetLayout);
    ~ShadowRenderSystem();

    ShadowRenderSystem(const ShadowRenderSystem&) = delete;
    ShadowRenderSystem& operator=(const ShadowRenderSystem&) = delete;

    // Рисует всю геометрию с заданной lightSpaceMatrix
void renderShadow(
    VkCommandBuffer commandBuffer,
    const glm::mat4& lightSpaceMatrix,
    CullingSystem& cullingSystem, 
    entt::registry& registry,     // <--- Возвращаем registry
    
    VkDescriptorSet objectStorageSet);

private:
    void createPipelineLayout(VkDescriptorSetLayout objectSetLayout);
    void createPipeline(VkRenderPass renderPass);

    BurnhopeDevice& lveDevice;
    std::unique_ptr<BurnhopePipeline> pipeline;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
};

} // namespace burnhope

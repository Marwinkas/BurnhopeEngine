#include "ShadowRender.hpp"
#include <stdexcept>
#include <iostream>

namespace burnhope {

ShadowRenderSystem::ShadowRenderSystem(BurnhopeDevice& device,
    VkRenderPass shadowRenderPass,
    VkDescriptorSetLayout globalSetLayout)
    : lveDevice(device)
{
    createPipelineLayout(globalSetLayout);
    createPipeline(shadowRenderPass);
}

ShadowRenderSystem::~ShadowRenderSystem() {
    vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

void ShadowRenderSystem::createPipelineLayout(VkDescriptorSetLayout objectSetLayout) {
    // Push constant: только lightSpaceMatrix (64 байта)
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(ShadowPushConstant);

    // Set 0: globalUbo (нужен для доступа к objectBuffer если он там)
    // Set 1: objectStorageSet (objectBuffer с modelMatrix)
    std::vector<VkDescriptorSetLayout> layouts = {
        objectSetLayout
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = (uint32_t)layouts.size();
    layoutInfo.pSetLayouts            = layouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;

    if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("Failed to create shadow pipeline layout!");
}

void ShadowRenderSystem::createPipeline(VkRenderPass renderPass) {
    PipelineConfigInfo config{};
    BurnhopePipeline::defaultPipelineConfigInfo(config);

    // Shadow pipeline: только depth, без цвета
    config.renderPass     = renderPass;
    config.pipelineLayout = pipelineLayout;

    // Нет color attachments
    config.colorBlendInfo.attachmentCount = 0;
    config.colorBlendInfo.pAttachments    = nullptr;

    // Depth bias для борьбы с shadow acne
    config.rasterizationInfo.depthBiasEnable         = VK_TRUE;
    config.rasterizationInfo.depthBiasConstantFactor = 1.25f;
    config.rasterizationInfo.depthBiasSlopeFactor    = 1.75f;

    // Culling: front face culling убирает peter-panning
    config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;

    pipeline = std::make_unique<BurnhopePipeline>(
        lveDevice,
        "shaders/shadow.vert.spv",
        "shaders/shadow.frag.spv",
        config);
}
void ShadowRenderSystem::renderShadow(
    VkCommandBuffer commandBuffer,
    const glm::mat4& lightSpaceMatrix,
    CullingSystem& cullingSystem, 
     entt::registry& registry,
    VkDescriptorSet objectStorageSet)
{
    pipeline->bind(commandBuffer);

    if (objectStorageSet == VK_NULL_HANDLE) return;

    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &objectStorageSet, 0, nullptr);

    ShadowPushConstant push{};
    push.lightSpaceMatrix = lightSpaceMatrix;
    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstant), &push);

    auto view = registry.view<TransformComponent, MeshComponent>();
    uint32_t instanceIndex = 0;

    // Проходим по моделям (как в G-Buffer)
    for (auto [entity, transformComp, meshComp] : view.each()) {
        if (!meshComp.model || !meshComp.isVisible) continue;

        // САМОЕ ГЛАВНОЕ: Биндим вершины и индексы для текущей модели!
        meshComp.model->bind(commandBuffer);

        const auto& subMeshes = meshComp.model->getSubMeshes();
        uint32_t subMeshCount = static_cast<uint32_t>(subMeshes.size());

        // Рисуем все кластеры ЭТОЙ модели за один вызов
        vkCmdDrawIndexedIndirect(
            commandBuffer,
            cullingSystem.getDrawCommandBuffer(),
            instanceIndex * sizeof(VkDrawIndexedIndirectCommand), // Сдвиг для нужных команд
            subMeshCount,
            sizeof(VkDrawIndexedIndirectCommand));

        instanceIndex += subMeshCount;
    }
}

} // namespace burnhope

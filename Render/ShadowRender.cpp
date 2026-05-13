#include "ShadowRender.hpp"
#include "GraphicsShader.hpp"
#include <stdexcept>
#include <iostream>
namespace burnhope
{
    ShadowRenderSystem::ShadowRenderSystem(BurnhopeDevice &device,
                                           VkRenderPass shadowRenderPass,
                                           VkDescriptorSetLayout globalSetLayout)
        : lveDevice(device)
    {
        PipelineConfigInfo config{};
        BurnhopePipeline::defaultPipelineConfigInfo(config);
        config.renderPass = shadowRenderPass;
        config.colorBlendInfo.attachmentCount = 0;
        config.colorBlendInfo.pAttachments = nullptr;
        config.rasterizationInfo.depthBiasEnable = VK_TRUE;
        config.rasterizationInfo.depthBiasConstantFactor = 1.25f;
        config.rasterizationInfo.depthBiasSlopeFactor = 1.75f;
        config.rasterizationInfo.cullMode = VK_CULL_MODE_FRONT_BIT;
        VkPushConstantRange pushRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowPushConstant)};
        shader = std::make_unique<GraphicsShader>(
            lveDevice,
            std::vector<std::string>{"shaders/shadow.vert.spv", "shaders/shadow.frag.spv"},
            std::vector<VkDescriptorSetLayout>{globalSetLayout},
            std::vector<VkPushConstantRange>{pushRange},
            config);
    }
    ShadowRenderSystem::~ShadowRenderSystem() = default;
void ShadowRenderSystem::renderShadow(
    VkCommandBuffer commandBuffer,
    const glm::mat4 &lightSpaceMatrix,
    entt::registry &registry,
            VkDescriptorSet objectStorageSet,
            bool renderDynamicOnly)
{
    if (objectStorageSet == VK_NULL_HANDLE)
        return;

    shader->bind(commandBuffer);
    shader->bindDescriptorSets(commandBuffer, {objectStorageSet});

    ShadowPushConstant push{lightSpaceMatrix};
    shader->pushConstants(commandBuffer, VK_SHADER_STAGE_VERTEX_BIT, sizeof(ShadowPushConstant), &push);

    auto view = registry.view<TransformComponent, MeshComponent>();
    
    // Очень важно: этот индекс должен совпадать с индексом объекта в твоем ObjectBuffer,
    // чтобы шейдер взял правильную матрицу модели (modelMatrix).
    uint32_t instanceIndex = 0; 

    for (auto [entity, transformComp, meshComp] : view.each())
    {
        if (!meshComp.model || !meshComp.isVisible)
            continue;
        if (renderDynamicOnly && meshComp.isStatic) 
            continue; // Пропускаем статические меши при локальном обновлении теней!
        meshComp.model->bind(commandBuffer);
        const auto &subMeshes = meshComp.model->getSubMeshes();

        for (const auto &sub : subMeshes)
        {
            // Вместо vkCmdDrawIndexedIndirect используем прямой вызов
            vkCmdDrawIndexed(
                commandBuffer, 
                sub.indexCounts[0], // Количество индексов (LOD 0)
                1,                  // Рисуем 1 экземпляр
                sub.firstIndices[0],// Смещение индексов
                0,                  // Смещение вершин
                instanceIndex       // gl_InstanceIndex в шейдере
            );
            
            instanceIndex++;
        }
    }
}
}
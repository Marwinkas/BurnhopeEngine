#include "MainRender.hpp"
#include "GraphicsShader.hpp"
#include <iostream>
namespace burnhope
{
    struct GeometryPushConstants
    {
        glm::mat4 modelMatrix{1.f};
        glm::mat4 normalMatrix{1.f};
    };
    GeometryRenderSystem::GeometryRenderSystem(
        BurnhopeDevice &device, VkRenderPass gBufferRenderPass, VkDescriptorSetLayout globalSetLayout)
        : lveDevice{device}
    {
        renderSystemLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                 .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
                                 .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT)
                                 .build();
        textureLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT, 1000)
                            .build();
        std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout,
            renderSystemLayout->getDescriptorSetLayout(),
            textureLayout->getDescriptorSetLayout()};

        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstants)};

        PipelineConfigInfo pipelineConfig{};
        BurnhopePipeline::defaultPipelineConfigInfo(pipelineConfig);
        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(4);
        for (int i = 0; i < 4; i++)
        {
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[i].blendEnable = VK_FALSE;
        }
        pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();
        pipelineConfig.renderPass = gBufferRenderPass;
        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstants)};
        shader = std::make_unique<GraphicsShader>(
            lveDevice,
            "shaders/gbuffer.vert.spv",
            "shaders/gbuffer.frag.spv",
            layouts,
            std::vector<VkPushConstantRange>{pushConstantRange},
            pipelineConfig);
    }
    pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();
    pipelineConfig.renderPass = gBufferRenderPass;

    pipelineConfig.depthStencilInfo.stencilTestEnable = VK_FALSE;

    shader = std::make_unique<GraphicsShader>(
        lveDevice, "shaders/gbuffer.vert.spv", "shaders/gbuffer.frag.spv",
        layouts, std::vector<VkPushConstantRange>{pushConstantRange}, pipelineConfig);

    pipelineConfig.depthStencilInfo.stencilTestEnable = VK_TRUE;
    pipelineConfig.depthStencilInfo.depthTestEnable = VK_TRUE;
    pipelineConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
    pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    pipelineConfig.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
    pipelineConfig.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    pipelineConfig.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    pipelineConfig.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    pipelineConfig.depthStencilInfo.front.compareMask = 0xFF;
    pipelineConfig.depthStencilInfo.front.writeMask = 0x00;
    pipelineConfig.depthStencilInfo.back = pipelineConfig.depthStencilInfo.front;

    pipelineConfig.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
    pipelineConfig.dynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(pipelineConfig.dynamicStateEnables.size());
    pipelineConfig.dynamicStateInfo.pDynamicStates = pipelineConfig.dynamicStateEnables.data();

    portalShader = std::make_unique<GraphicsShader>(
        lveDevice, "shaders/gbuffer.vert.spv", "shaders/gbuffer.frag.spv",
        layouts, std::vector<VkPushConstantRange>{pushConstantRange}, pipelineConfig);
    }
    void GeometryRenderSystem::renderEntities(
        FrameInfo &frameInfo,
        entt::registry &registry,
        VkDescriptorSet storageSet,
        VkDescriptorSet textureSet,
        CullingSystem &cullingSystem,
        uint32_t totalSubMeshCount,
        bool useStencil)
    {
        if (storageSet == VK_NULL_HANDLE || textureSet == VK_NULL_HANDLE)
            return;

        GraphicsShader *currentShader = useStencil ? portalShader.get() : shader.get();

        currentShader->bind(frameInfo.commandBuffer);

        std::vector<VkDescriptorSet> sets = {frameInfo.globalDescriptorSet, storageSet, textureSet};
        currentShader->bindDescriptorSets(frameInfo.commandBuffer, sets);

        auto view = registry.view<TransformComponent, MeshComponent>();
        uint32_t instanceIndex = 0;

        for (auto [entity, transformComp, meshComp] : view.each())
        {
            if (!meshComp.model || !meshComp.isVisible)
                continue;

            meshComp.model->bind(frameInfo.commandBuffer);
            const auto &subMeshes = meshComp.model->getSubMeshes();
            uint32_t subMeshCount = static_cast<uint32_t>(subMeshes.size());

            vkCmdDrawIndexedIndirect(
                frameInfo.commandBuffer,
                cullingSystem.getDrawCommandBuffer(),
                instanceIndex * sizeof(VkDrawIndexedIndirectCommand),
                subMeshCount,
                sizeof(VkDrawIndexedIndirectCommand));

            instanceIndex += subMeshCount;
        }
    }
}
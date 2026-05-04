#include "MainRender.hpp"
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
        createPipelineLayout(globalSetLayout);
        createPipeline(gBufferRenderPass);
    }
    void GeometryRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout)
    {
        renderSystemLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                                 .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT)
                                 .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)
                                 .build();
        textureLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1000)
                            .build();
        std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout,
            renderSystemLayout->getDescriptorSetLayout(),
            textureLayout->getDescriptorSetLayout()};
        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
        pipelineLayoutInfo.pSetLayouts = layouts.data();
        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstants)};
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
        if (vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create pipeline layout!");
        }
    }
    void GeometryRenderSystem::createPipeline(VkRenderPass renderPass)
    {
        assert(pipelineLayout != nullptr && "Cannot create pipeline before pipeline layout");
        PipelineConfigInfo pipelineConfig{};
        BurnhopePipeline::defaultPipelineConfigInfo(pipelineConfig);
        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(3);
        for (int i = 0; i < 3; i++)
        {
            blendAttachments[i].colorWriteMask =
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[i].blendEnable = VK_FALSE;
        }
        pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();
        pipelineConfig.renderPass = renderPass;
        pipelineConfig.pipelineLayout = pipelineLayout;
        lvePipeline = std::make_unique<BurnhopePipeline>(
            lveDevice,
            "shaders/gbuffer.vert.spv",
            "shaders/gbuffer.frag.spv",
            pipelineConfig);
    }
    void GeometryRenderSystem::renderEntities(
        FrameInfo &frameInfo,
        entt::registry &registry,
        VkDescriptorSet storageSet,
        VkDescriptorSet textureSet,
        CullingSystem &cullingSystem,
        uint32_t totalSubMeshCount)
    {
        if (storageSet == VK_NULL_HANDLE || textureSet == VK_NULL_HANDLE)
            return;
        lvePipeline->bind(frameInfo.commandBuffer);
        std::vector<VkDescriptorSet> sets = {
            frameInfo.globalDescriptorSet, storageSet, textureSet};
        vkCmdBindDescriptorSets(frameInfo.commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                                0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
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
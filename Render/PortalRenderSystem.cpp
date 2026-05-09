#include "PortalRenderSystem.hpp"
#include <stdexcept>

namespace burnhope
{
    struct PortalPushArgs
    {
        glm::mat4 modelMatrix{1.f};
    };

    PortalRenderSystem::PortalRenderSystem(BurnhopeDevice &device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout)
        : lveDevice{device}
    {
        createPipelineLayout(globalSetLayout);
        createPipeline(renderPass);
        createDepthResetPipeline(renderPass);

        std::vector<Vertex> vertices = {
            {{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}},
            {{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}},
            {{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}},
            {{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}}};
        std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

        Builder builder;
        builder.vertices = vertices;
        builder.indices = indices;

        SubMesh sub;
        sub.indexCounts[0] = static_cast<uint32_t>(indices.size());
        sub.firstIndices[0] = 0;
        builder.subMeshes.push_back(sub);

        portalModel = std::make_unique<BurnhopeModel>(lveDevice, builder);
    }

    PortalRenderSystem::~PortalRenderSystem()
    {
        vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
    }

    void PortalRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout)
    {
        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PortalPushArgs)};
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &globalSetLayout;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
            throw std::runtime_error("Portal Layout Error");
    }

    void PortalRenderSystem::createPipeline(VkRenderPass renderPass)
    {
        PipelineConfigInfo config{};
        BurnhopePipeline::defaultPipelineConfigInfo(config);

        config.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
        config.dynamicStateInfo.dynamicStateCount =
            static_cast<uint32_t>(config.dynamicStateEnables.size());
        config.dynamicStateInfo.pDynamicStates = config.dynamicStateEnables.data();

        config.attributeDescriptions.clear();
        config.bindingDescriptions.clear();

        config.bindingDescriptions = Vertex::getBindingDescriptions();
        config.attributeDescriptions = Vertex::getAttributeDescriptions();

        config.renderPass = renderPass;
        config.pipelineLayout = pipelineLayout;

        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(3);
        for (int i = 0; i < 3; i++)
        {
            blendAttachments[i].blendEnable = VK_FALSE;
            blendAttachments[i].colorWriteMask = 0;
        }

        config.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        config.colorBlendInfo.pAttachments = blendAttachments.data();

        config.depthStencilInfo.depthTestEnable = VK_TRUE;
        config.depthStencilInfo.depthWriteEnable = VK_TRUE;
        config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        config.depthStencilInfo.stencilTestEnable = VK_TRUE;
        config.depthStencilInfo.front.passOp = VK_STENCIL_OP_REPLACE;
        config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;

        config.depthStencilInfo.front.compareMask = 0xFF;
        config.depthStencilInfo.front.writeMask = 0xFF;
        config.depthStencilInfo.front.reference = 1;
        config.depthStencilInfo.back = config.depthStencilInfo.front;

        lvePipeline = std::make_unique<BurnhopePipeline>(
            lveDevice, "shaders/portal.vert.spv", "shaders/portal.frag.spv", config);
    }

    void PortalRenderSystem::createDepthResetPipeline(VkRenderPass renderPass) {
        PipelineConfigInfo config{};
        BurnhopePipeline::defaultPipelineConfigInfo(config);

        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(3);
        for (int i = 0; i < 3; i++) {
            blendAttachments[i].blendEnable = VK_FALSE;
            blendAttachments[i].colorWriteMask = 0;
        }
        config.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        config.colorBlendInfo.pAttachments = blendAttachments.data();

        config.depthStencilInfo.depthTestEnable = VK_TRUE;
        config.depthStencilInfo.depthWriteEnable = VK_TRUE;
        config.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_ALWAYS;

        config.depthStencilInfo.stencilTestEnable = VK_TRUE;
        config.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
        config.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
        config.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
        config.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
        config.depthStencilInfo.front.compareMask = 0xFF;
        config.depthStencilInfo.front.writeMask = 0x00;
        config.depthStencilInfo.back = config.depthStencilInfo.front;

        config.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
        config.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(config.dynamicStateEnables.size());
        config.dynamicStateInfo.pDynamicStates = config.dynamicStateEnables.data();

        config.renderPass = renderPass;
        config.pipelineLayout = pipelineLayout;

        depthResetPipeline = std::make_unique<BurnhopePipeline>(
            lveDevice, "shaders/portal.vert.spv", "shaders/depth_reset.frag.spv", config);
    }

    void PortalRenderSystem::drawDepthReset(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4 &model, uint32_t ref) {
        depthResetPipeline->bind(cmd);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        
        PortalPushArgs args{model};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PortalPushArgs), &args);
        
        portalModel->bind(cmd);
        portalModel->draw(cmd);
    }

    void PortalRenderSystem::drawMask(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4 &model, uint32_t ref)
    {
        lvePipeline->bind(cmd);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        PortalPushArgs args{model};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PortalPushArgs), &args);
        portalModel->bind(cmd);
        portalModel->draw(cmd);
    }
}
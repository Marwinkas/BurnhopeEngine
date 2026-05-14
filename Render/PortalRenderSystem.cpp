#include "PortalRenderSystem.hpp"
#include <stdexcept>

namespace burnhope
{
    struct PortalPushArgs
    {
        glm::mat4 modelMatrix{1.f};
        uint32_t portalID;
    };

    PortalRenderSystem::PortalRenderSystem(BurnhopeDevice &device, VkDescriptorSetLayout globalSetLayout)
        : lveDevice{device}
    {
        createPipelineLayout(globalSetLayout);
        std::vector<PackedVertexPos> positions = {
            {0, 0, 0, 0},
            {65535, 0, 0, 0},
            {65535, 65535, 0, 0},
            {0, 65535, 0, 0}
        };

        uint32_t qTan = glm::packSnorm3x10_1x2(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<PackedVertexAttr> attributes = {
            {glm::packHalf2x16(glm::vec2(0.f, 0.f)), qTan},
            {glm::packHalf2x16(glm::vec2(1.f, 0.f)), qTan},
            {glm::packHalf2x16(glm::vec2(1.f, 1.f)), qTan},
            {glm::packHalf2x16(glm::vec2(0.f, 1.f)), qTan}
        };

        std::vector<uint32_t> indices = {0, 1, 2, 2, 3, 0};

        Builder builder;
        builder.positions = positions;
        builder.attributes = attributes;
        builder.indices = indices;

        SubMesh sub;
        sub.indexCounts[0] = static_cast<uint32_t>(indices.size());
        sub.firstIndices[0] = 0;
        sub.aabbMin = glm::vec3(-1.f, -1.f, 0.f);
        sub.aabbMax = glm::vec3(1.f, 1.f, 0.f);
        sub.boundingRadius = glm::distance(sub.aabbMin, sub.aabbMax) * 0.5f;
        builder.subMeshes.push_back(sub);

        portalModel = std::make_unique<BurnhopeModel>(lveDevice, builder);
    }

    PortalRenderSystem::~PortalRenderSystem()
    {
        vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
    }

    void PortalRenderSystem::createPipelineLayout(VkDescriptorSetLayout globalSetLayout)
    {
        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalPushArgs)};
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

        config.colorAttachmentFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R8_UINT};
        config.depthAttachmentFormat = lveDevice.findSupportedFormat({VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        config.stencilAttachmentFormat = config.depthAttachmentFormat;
        config.pipelineLayout = pipelineLayout;

        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(5);
        for (int i = 0; i < 5; i++)
        {
            blendAttachments[i].blendEnable = VK_FALSE;
            blendAttachments[i].colorWriteMask = 0;
        }
        // Enable writing to the 5th attachment (portalID)
        blendAttachments[4].colorWriteMask = VK_COLOR_COMPONENT_R_BIT;

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
            lveDevice, std::vector<std::string>{"shaders/portal.vert.spv", "shaders/portal.frag.spv"}, config);
    }

    void PortalRenderSystem::createDepthResetPipeline(VkRenderPass renderPass) {
        PipelineConfigInfo config{};
        BurnhopePipeline::defaultPipelineConfigInfo(config);

        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(5);
        for (int i = 0; i < 5; i++) {
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

        config.colorAttachmentFormats = {
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_R8_UINT};
        config.depthAttachmentFormat = lveDevice.findSupportedFormat({VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}, VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        config.stencilAttachmentFormat = config.depthAttachmentFormat;
        config.pipelineLayout = pipelineLayout;

        depthResetPipeline = std::make_unique<BurnhopePipeline>(
            lveDevice, std::vector<std::string>{"shaders/portal.vert.spv", "shaders/depth_reset.frag.spv"}, config);
    }

    void PortalRenderSystem::drawDepthReset(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4 &model, uint32_t ref) {
        depthResetPipeline->bind(cmd);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        
        PortalPushArgs args{model, ref};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalPushArgs), &args);
        
        portalModel->bind(cmd);
        portalModel->draw(cmd);
    }

    void PortalRenderSystem::drawMask(VkCommandBuffer cmd, VkDescriptorSet globalSet, const glm::mat4 &model, uint32_t ref)
    {
        lvePipeline->bind(cmd);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ref);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalSet, 0, nullptr);
        PortalPushArgs args{model, ref};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PortalPushArgs), &args);
        portalModel->bind(cmd);
        portalModel->draw(cmd);
    }
}
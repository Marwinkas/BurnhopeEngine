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
                                 .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
                                 .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_ALL)
                                 .addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_TASK_BIT_EXT)
                                 .build();
        textureLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
                            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL, 1000)
                            .build();
        std::vector<VkDescriptorSetLayout> layouts = {
            globalSetLayout,
            renderSystemLayout->getDescriptorSetLayout(),
            textureLayout->getDescriptorSetLayout()};

        VkPushConstantRange pushConstantRange{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(GeometryPushConstants)};
        PipelineConfigInfo pipelineConfig{};
        BurnhopePipeline::defaultPipelineConfigInfo(pipelineConfig);
        static std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(5);
        for (int i = 0; i < 5; i++)
        {
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
            blendAttachments[i].blendEnable = VK_FALSE;
        }
        pipelineConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
        pipelineConfig.colorBlendInfo.pAttachments = blendAttachments.data();
        pipelineConfig.renderPass = gBufferRenderPass;
        pipelineConfig.depthStencilInfo.stencilTestEnable = VK_FALSE;

        shader = std::make_unique<GraphicsShader>(
            lveDevice,
            std::vector<std::string>{"shaders/gbuffer.task.spv", "shaders/gbuffer.mesh.spv", "shaders/gbuffer.frag.spv"},
            layouts,
            std::vector<VkPushConstantRange>{pushConstantRange},
            pipelineConfig);


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

        // --- Z-PREPASS CONFIG ---
        PipelineConfigInfo zConfig = pipelineConfig;
        static std::vector<VkPipelineColorBlendAttachmentState> zBlendAttachments(5);
        for (int i = 0; i < 5; i++)
        {
            zBlendAttachments[i].colorWriteMask = 0; // Строго запрещаем запись цвета!
            zBlendAttachments[i].blendEnable = VK_FALSE;
        }
        zConfig.colorBlendInfo.attachmentCount = static_cast<uint32_t>(zBlendAttachments.size());
        zConfig.colorBlendInfo.pAttachments = zBlendAttachments.data();
        zConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
        zConfig.depthStencilInfo.depthWriteEnable = VK_TRUE;
        zPrepassShader = std::make_unique<GraphicsShader>(lveDevice, std::vector<std::string>{"shaders/gbuffer.task.spv", "shaders/gbuffer.mesh.spv"}, layouts, std::vector<VkPushConstantRange>{pushConstantRange}, zConfig);

        // --- G-BUFFER MAIN CONFIG ---
        pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_EQUAL;
        pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;



        pipelineConfig.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR);
        pipelineConfig.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
        pipelineConfig.dynamicStateInfo.dynamicStateCount =
            static_cast<uint32_t>(pipelineConfig.dynamicStateEnables.size());
        pipelineConfig.dynamicStateInfo.pDynamicStates = pipelineConfig.dynamicStateEnables.data();

        portalShader = std::make_unique<GraphicsShader>(
            lveDevice, std::vector<std::string>{"shaders/gbuffer.task.spv", "shaders/gbuffer.mesh.spv", "shaders/gbuffer.frag.spv"},
            layouts, std::vector<VkPushConstantRange>{pushConstantRange}, pipelineConfig);
        zConfig.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
        zConfig.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(zConfig.dynamicStateEnables.size());
        zConfig.dynamicStateInfo.pDynamicStates = zConfig.dynamicStateEnables.data();
        zPrepassPortalShader = std::make_unique<GraphicsShader>(lveDevice, std::vector<std::string>{"shaders/gbuffer.task.spv", "shaders/gbuffer.mesh.spv"}, layouts, std::vector<VkPushConstantRange>{pushConstantRange}, zConfig);
    }

    void GeometryRenderSystem::renderEntities(
        FrameInfo &frameInfo,
        entt::registry &registry,
        VkDescriptorSet storageSet,
        VkDescriptorSet textureSet,
        uint32_t totalSubMeshCount,
        bool useStencil,
        uint32_t vrsMode,
        bool isZPrepass)
    {
        if (storageSet == VK_NULL_HANDLE || textureSet == VK_NULL_HANDLE)
            return;

        auto pfnCmdSetFragmentShadingRateKHR = (PFN_vkCmdSetFragmentShadingRateKHR)vkGetDeviceProcAddr(lveDevice.device(), "vkCmdSetFragmentShadingRateKHR");
        if (pfnCmdSetFragmentShadingRateKHR) {
            VkExtent2D fragmentSize = {1, 1};
            if (vrsMode == 1) fragmentSize = {2, 2}; // 2x2 пикселя считаются как 1
            
            VkFragmentShadingRateCombinerOpKHR combinerOps[2] = {
                VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR, 
                VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR
            };
            pfnCmdSetFragmentShadingRateKHR(frameInfo.commandBuffer, &fragmentSize, combinerOps);
        }

         GraphicsShader *currentShader;
        if (isZPrepass) {
            currentShader = useStencil ? zPrepassPortalShader.get() : zPrepassShader.get();
        } else {
            currentShader = useStencil ? portalShader.get() : shader.get();
        }
        currentShader->bind(frameInfo.commandBuffer);

        std::vector<VkDescriptorSet> sets = {frameInfo.globalDescriptorSet, storageSet, textureSet};
        currentShader->bindDescriptorSets(frameInfo.commandBuffer, sets);

        auto vkCmdDrawMeshTasksEXT = (PFN_vkCmdDrawMeshTasksEXT)vkGetDeviceProcAddr(lveDevice.device(), "vkCmdDrawMeshTasksEXT");
        if (vkCmdDrawMeshTasksEXT) {
            // 1 Task Workgroup = 1 объект сцены.
            if (totalSubMeshCount > 0) {
                vkCmdDrawMeshTasksEXT(frameInfo.commandBuffer, totalSubMeshCount, 1, 1);
            }
        }
    }
}
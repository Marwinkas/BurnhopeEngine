
#include "GTAOSystem.hpp"
#include <stdexcept>
#include <fstream>

namespace burnhope {

GTAOSystem::GTAOSystem(BurnhopeDevice& device, const std::vector<VkDescriptorSetLayout>& layouts) : lveDevice(device) {
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    layoutInfo.pSetLayouts = layouts.data();

    if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create GTAO pipeline layout!");
    }

    
    std::ifstream file("shaders/gtao.comp.spv", std::ios::binary);
    std::vector<char> code((std::istreambuf_iterator<char>(file)), {});
    VkShaderModuleCreateInfo shaderInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    shaderInfo.codeSize = code.size();
    shaderInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    vkCreateShaderModule(lveDevice.device(), &shaderInfo, nullptr, &shaderModule);

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule;
    pipelineInfo.stage.pName = "main";

    vkCreateComputePipelines(lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(lveDevice.device(), shaderModule, nullptr);
}

GTAOSystem::~GTAOSystem() {
    vkDestroyPipeline(lveDevice.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}
void GTAOSystem::compute(VkCommandBuffer cmd, VkDescriptorSet globalSet, VkDescriptorSet gtaoSet, uint32_t width, uint32_t height) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    
    
    VkDescriptorSet sets[] = { globalSet, gtaoSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 2, sets, 0, nullptr);

    vkCmdDispatch(cmd, (width + 15) / 16, (height + 15) / 16, 1);
}
}
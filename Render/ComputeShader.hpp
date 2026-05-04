#pragma once
#include "../Utils/Device.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
namespace burnhope
{
    class ComputeShader
    {
    public:
        ComputeShader(BurnhopeDevice &device,
                      const std::string &shaderPath,
                      const std::vector<VkDescriptorSetLayout> &layouts,
                      uint32_t pushConstantSize = 0)
            : lveDevice(device)
        {
            createPipelineLayout(layouts, pushConstantSize);
            createPipeline(shaderPath);
        }
        ~ComputeShader()
        {
            vkDestroyPipeline(lveDevice.device(), pipeline, nullptr);
            vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
        }
        void bind(VkCommandBuffer cmd)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        }
        void bindDescriptorSets(VkCommandBuffer cmd, const std::vector<VkDescriptorSet> &sets)
        {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0,
                                    static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
        }
        void pushConstants(VkCommandBuffer cmd, const void *data, uint32_t size)
        {
            vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, size, data);
        }
        void dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ = 1)
        {
            vkCmdDispatch(cmd, groupX, groupY, groupZ);
        }

    private:
        BurnhopeDevice &lveDevice;
        VkPipelineLayout pipelineLayout;
        VkPipeline pipeline;
        void createPipelineLayout(const std::vector<VkDescriptorSetLayout> &layouts, uint32_t pushConstantSize)
        {
            VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
            layoutInfo.pSetLayouts = layouts.data();
            VkPushConstantRange pushRange{};
            if (pushConstantSize > 0)
            {
                pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                pushRange.size = pushConstantSize;
                layoutInfo.pushConstantRangeCount = 1;
                layoutInfo.pPushConstantRanges = &pushRange;
            }
            if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
                throw std::runtime_error("Failed to create compute pipeline layout!");
        }
        void createPipeline(const std::string &shaderPath)
        {
            std::ifstream file(shaderPath, std::ios::binary);
            std::vector<char> code((std::istreambuf_iterator<char>(file)), {});
            VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            shaderInfo.codeSize = code.size();
            shaderInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
            VkShaderModule shaderModule;
            vkCreateShaderModule(lveDevice.device(), &shaderInfo, nullptr, &shaderModule);
            VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            pipelineInfo.layout = pipelineLayout;
            pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                                  VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "main", nullptr};
            vkCreateComputePipelines(lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
            vkDestroyShaderModule(lveDevice.device(), shaderModule, nullptr);
        }
    };
}
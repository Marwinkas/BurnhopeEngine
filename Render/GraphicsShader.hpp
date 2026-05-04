#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Pipeline.hpp"
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
namespace burnhope
{
    class GraphicsShader
    {
    public:
        GraphicsShader(BurnhopeDevice &device,
                       const std::string &vertPath,
                       const std::string &fragPath,
                       const std::vector<VkDescriptorSetLayout> &layouts,
                       const std::vector<VkPushConstantRange> &pushRanges,
                       PipelineConfigInfo &config)
            : lveDevice(device)
        {
            VkPipelineLayoutCreateInfo layoutInfo{};
            layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            layoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
            layoutInfo.pSetLayouts = layouts.data();
            layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushRanges.size());
            layoutInfo.pPushConstantRanges = pushRanges.data();
            if (vkCreatePipelineLayout(lveDevice.device(), &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
            {
                throw std::runtime_error("Не удалось создать pipeline layout!");
            }
            config.pipelineLayout = pipelineLayout;
            pipeline = std::make_unique<BurnhopePipeline>(device, vertPath, fragPath, config);
        }
        ~GraphicsShader()
        {
            vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
        }
        void bind(VkCommandBuffer cmd)
        {
            pipeline->bind(cmd);
        }
        void bindDescriptorSets(VkCommandBuffer cmd, const std::vector<VkDescriptorSet> &sets)
        {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0,
                                    static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);
        }
        void pushConstants(VkCommandBuffer cmd, VkShaderStageFlags stage, uint32_t size, const void *data)
        {
            vkCmdPushConstants(cmd, pipelineLayout, stage, 0, size, data);
        }

    private:
        BurnhopeDevice &lveDevice;
        VkPipelineLayout pipelineLayout;
        std::unique_ptr<BurnhopePipeline> pipeline;
    };
}
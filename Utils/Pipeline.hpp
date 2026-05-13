#pragma once
#include "Device.hpp"
#include <string>
#include <vector>
namespace burnhope
{
    struct PipelineConfigInfo
    {
        PipelineConfigInfo() = default;
        std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
        std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
        VkPipelineViewportStateCreateInfo viewportInfo;
        VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo;
        VkPipelineRasterizationStateCreateInfo rasterizationInfo;
        VkPipelineMultisampleStateCreateInfo multisampleInfo;
        VkPipelineColorBlendAttachmentState colorBlendAttachment;
        VkPipelineColorBlendStateCreateInfo colorBlendInfo;
        VkPipelineDepthStencilStateCreateInfo depthStencilInfo;
        std::vector<VkDynamicState> dynamicStateEnables;
        VkPipelineDynamicStateCreateInfo dynamicStateInfo;
        VkPipelineLayout pipelineLayout = nullptr;
        VkRenderPass renderPass = nullptr;
        uint32_t subpass = 0;
    };
    class BurnhopePipeline
    {
    public:
        BurnhopePipeline(
            BurnhopeDevice &device,
            const std::vector<std::string>& shaderPaths,
            const PipelineConfigInfo &configInfo);
        ~BurnhopePipeline();
        BurnhopePipeline(
            BurnhopeDevice &device,
            const std::string &compFilepath,
            VkPipelineLayout pipelineLayout);
        void bindCompute(VkCommandBuffer commandBuffer);
        void createComputePipeline(const std::string &compFilepath, VkPipelineLayout pipelineLayout);
        VkShaderModule compShaderModule = VK_NULL_HANDLE;
        VkPipeline computePipeline = VK_NULL_HANDLE;
        BurnhopePipeline(const BurnhopePipeline &) = delete;
        BurnhopePipeline &operator=(const BurnhopePipeline &) = delete;
        void bind(VkCommandBuffer commandBuffer);
        static void defaultPipelineConfigInfo(PipelineConfigInfo &configInfo);
        static void enableAlphaBlending(PipelineConfigInfo &configInfo);
    private:
        static std::vector<char> readFile(const std::string &filepath);
        void createGraphicsPipeline(
            const std::vector<std::string>& shaderPaths,
            const PipelineConfigInfo &configInfo);
        void createShaderModule(const std::vector<char> &code, VkShaderModule *shaderModule);
        BurnhopeDevice &lveDevice;
        VkPipeline graphicsPipeline;
        std::vector<VkShaderModule> shaderModules;
    };
}

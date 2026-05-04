#pragma once
#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <functional>
namespace burnhope
{
    struct RenderPassNode
    {
        std::string name;
        std::vector<VkImageMemoryBarrier> barriersBefore;
        std::function<void(VkCommandBuffer)> executeFunction;
    };
    class RenderGraph
    {
    public:
        void addPass(const std::string &name,
                     const std::vector<VkImageMemoryBarrier> &barriers,
                     std::function<void(VkCommandBuffer)> execute)
        {
            passes.push_back({name, barriers, execute});
        }
        void execute(VkCommandBuffer commandBuffer)
        {
            for (const auto &pass : passes)
            {
                if (!pass.barriersBefore.empty())
                {
                    vkCmdPipelineBarrier(
                        commandBuffer,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                        0,
                        0, nullptr,
                        0, nullptr,
                        static_cast<uint32_t>(pass.barriersBefore.size()),
                        pass.barriersBefore.data());
                }
                if (pass.executeFunction)
                {
                    pass.executeFunction(commandBuffer);
                }
            }
        }
        void clear()
        {
            passes.clear();
        }

    private:
        std::vector<RenderPassNode> passes;
    };
}
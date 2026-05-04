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
    class RenderPipeline
    {
    public:
        static VkImageMemoryBarrier createImageBarrier(
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkAccessFlags srcAccess,
            VkAccessFlags dstAccess,
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            uint32_t layerCount = 1,
            uint32_t mipLevels = 1)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {aspectMask, 0, mipLevels, 0, layerCount};
            barrier.srcAccessMask = srcAccess;
            barrier.dstAccessMask = dstAccess;
            return barrier;
        }
        void addPass(const std::string &name,
                     const std::vector<VkImageMemoryBarrier> &barriers,
                     std::function<void(VkCommandBuffer)> execute)
        {
            passes.push_back({name, barriers, execute});
        }
        void addPass(const std::string &name, std::function<void(VkCommandBuffer)> execute)
        {
            passes.push_back({name, {}, execute});
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
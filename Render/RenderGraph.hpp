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
        std::vector<VkImageMemoryBarrier2> barriersBefore;
        std::function<void(VkCommandBuffer)> executeFunction;
    };
    class RenderPipeline
    {
    public:
        static VkImageMemoryBarrier2 createImageBarrier(
            VkImage image,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            VkPipelineStageFlags2 srcStage,
            VkAccessFlags2 srcAccess,
            VkPipelineStageFlags2 dstStage,
            VkAccessFlags2 dstAccess,
            VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            uint32_t layerCount = 1,
            uint32_t mipLevels = 1)
        {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange = {aspectMask, 0, mipLevels, 0, layerCount};
            barrier.srcStageMask = srcStage;
            barrier.srcAccessMask = srcAccess;
            barrier.dstStageMask = dstStage;
            barrier.dstAccessMask = dstAccess;
            return barrier;
        }
        void addPass(const std::string &name,
                     const std::vector<VkImageMemoryBarrier2> &barriers,
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
                    VkDependencyInfo depInfo{};
                    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                    depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(pass.barriersBefore.size());
                    depInfo.pImageMemoryBarriers = pass.barriersBefore.data();
                    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
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
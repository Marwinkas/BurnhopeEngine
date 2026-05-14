#include "Gbuffer.hpp"
#include <array>
#include <stdexcept>
namespace burnhope
{
    BurnhopeGBuffer::BurnhopeGBuffer(BurnhopeDevice &device, VkExtent2D windowExtent)
        : lveDevice{device}, extent{windowExtent}
    {
        createResources();
    }
    BurnhopeGBuffer::~BurnhopeGBuffer()
    {
    }
    void BurnhopeGBuffer::createResources()
    {
        VkExtent3D ext3D = {extent.width, extent.height, 1};
        VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        normalRoughness = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        albedoMetallic = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R8G8B8A8_UNORM, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        heightAO = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        gEmissive = std::make_unique<BurnhopeTexture>(
            lveDevice, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
    gPortalID = std::make_unique<BurnhopeTexture>(
        lveDevice, VK_FORMAT_R8_UINT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        VkFormat depthFormat = lveDevice.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        depthTexture = std::make_unique<BurnhopeTexture>(
            lveDevice, depthFormat, ext3D, depthUsage, VK_SAMPLE_COUNT_1_BIT);
        VkCommandBuffer cmd = lveDevice.beginSingleTimeCommands();
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthTexture->getImage();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        barrier.srcAccessMask = 0;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
        
        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &depInfo);
        lveDevice.endSingleTimeCommands(cmd);
    }
   
}
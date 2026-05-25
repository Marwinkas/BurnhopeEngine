#include "Gbuffer.hpp"
#include <array>
#include <stdexcept>
namespace burnhope
{
    BurnhopeGBuffer::BurnhopeGBuffer(BurnhopeDevice &device, VkExtent2D windowExtent)
        : device_{device}, extent{windowExtent}
    {
        createResources();
    }
    BurnhopeGBuffer::~BurnhopeGBuffer()
    {
    }
    void BurnhopeGBuffer::createResources()
    {
        VkExtent3D ext3D = {extent.width, extent.height, 1};
        VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        VkImageUsageFlags depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        normalRoughness = std::make_unique<BurnhopeTexture>(
            device_, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        albedoMetallic = std::make_unique<BurnhopeTexture>(
            device_, VK_FORMAT_R8G8B8A8_UNORM, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        heightAO = std::make_unique<BurnhopeTexture>(
            device_, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        gEmissive = std::make_unique<BurnhopeTexture>(
            device_, VK_FORMAT_R16G16B16A16_SFLOAT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
    gPortalID = std::make_unique<BurnhopeTexture>(
        device_, VK_FORMAT_R8_UINT, ext3D, colorUsage, VK_SAMPLE_COUNT_1_BIT);
        VkFormat depthFormat = device_.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        depthTexture = std::make_unique<BurnhopeTexture>(
            device_, depthFormat, ext3D, depthUsage, VK_SAMPLE_COUNT_1_BIT);
        const std::array colorImages = {
            normalRoughness->getImage(),
            albedoMetallic->getImage(),
            heightAO->getImage(),
            gEmissive->getImage(),
            gPortalID->getImage(),
        };
        VkCommandBuffer cmd = device_.beginSingleTimeCommands();
        std::array<VkImageMemoryBarrier2, 6> barriers{};
        uint32_t barrierCount = 0;
        for (VkImage image : colorImages) {
            VkImageMemoryBarrier2& b = barriers[barrierCount++];
            b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.image = image;
            b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        }
        VkImageMemoryBarrier2& depthBarrier = barriers[barrierCount++];
        depthBarrier = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthBarrier.image = depthTexture->getImage();
        depthBarrier.subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
        depthBarrier.dstStageMask =
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        depthBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;

        VkDependencyInfo depInfo{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        depInfo.imageMemoryBarrierCount = barrierCount;
        depInfo.pImageMemoryBarriers = barriers.data();
        vkCmdPipelineBarrier2(cmd, &depInfo);
        device_.endSingleTimeCommands(cmd);
    }
   
}
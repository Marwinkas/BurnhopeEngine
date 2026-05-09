#include "Gbuffer.hpp"
#include <array>
#include <stdexcept>
namespace burnhope
{
    BurnhopeGBuffer::BurnhopeGBuffer(BurnhopeDevice &device, VkExtent2D windowExtent)
        : lveDevice{device}, extent{windowExtent}
    {
        createResources();
        createRenderPass();
        createFramebuffer();
    }
    BurnhopeGBuffer::~BurnhopeGBuffer()
    {
        vkDestroyFramebuffer(lveDevice.device(), framebuffer, nullptr);
        vkDestroyRenderPass(lveDevice.device(), renderPass, nullptr);
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
        VkFormat depthFormat = lveDevice.findSupportedFormat(
            {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
        depthTexture = std::make_unique<BurnhopeTexture>(
            lveDevice, depthFormat, ext3D, depthUsage, VK_SAMPLE_COUNT_1_BIT);
        VkCommandBuffer cmd = lveDevice.beginSingleTimeCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthTexture->getImage();
        barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
        lveDevice.endSingleTimeCommands(cmd);
    }
    void BurnhopeGBuffer::createRenderPass()
    {
        std::array<VkAttachmentDescription, 4> attachments{};
        for (int i = 0; i < 3; i++)
        {
            attachments[i].format = (i == 1) ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
            attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        attachments[3].format = depthTexture->getFormat();
        attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[3].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[3].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        std::array<VkAttachmentReference, 3> colorRefs{};
        colorRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        colorRefs[2] = {2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = static_cast<uint32_t>(colorRefs.size());
        subpass.pColorAttachments = colorRefs.data();
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        if (vkCreateRenderPass(lveDevice.device(), &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create G-Buffer render pass!");
        }
    }
    void BurnhopeGBuffer::createFramebuffer()
    {
        std::array<VkImageView, 4> attachments = {
            normalRoughness->getImageView(),
            albedoMetallic->getImageView(),
            heightAO->getImageView(),
            depthTexture->getImageView()};
        VkFramebufferCreateInfo fboInfo{};
        fboInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fboInfo.renderPass = renderPass;
        fboInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fboInfo.pAttachments = attachments.data();
        fboInfo.width = extent.width;
        fboInfo.height = extent.height;
        fboInfo.layers = 1;
        if (vkCreateFramebuffer(lveDevice.device(), &fboInfo, nullptr, &framebuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create G-Buffer framebuffer!");
        }
    }
}
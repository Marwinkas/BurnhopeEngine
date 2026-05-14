#include "Texture.hpp"
#include <stb_image.h>
#include <stb/stb_image_resize2.h>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <filesystem>
#include <ispc_texcomp.h>
namespace burnhope
{
    float BurnhopeTexture::GlobalAnisotropy = 16.0f;
    std::unordered_map<std::string, std::weak_ptr<BurnhopeTexture>> BurnhopeTexture::textureCache;

    BurnhopeTexture::BurnhopeTexture(BurnhopeDevice &device, const std::string &textureFilepath, bool isSRGB) : mDevice{device}
    {
        createTextureImage(textureFilepath, isSRGB);
        createTextureImageView(VK_IMAGE_VIEW_TYPE_2D);
        createTextureSampler();
        updateDescriptor();
    }
        BurnhopeTexture::BurnhopeTexture(
        BurnhopeDevice &device,
        VmaAllocation aliasedMemory,
        VkFormat format,
        VkExtent3D extent,
        VkImageUsageFlags usage,
        VkSampleCountFlagBits sampleCount)
        : mDevice{device}
    {
        mFormat = format;
        mExtent = extent;
        VkImageAspectFlags aspectMask = (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = sampleCount;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        // VMA Aliasing: Переиспользуем существующую память!
        vmaCreateAliasingImage(mDevice.getAllocator(), aliasedMemory, &imageInfo, &mTextureImage);
        mTextureImageMemory = VK_NULL_HANDLE; // Мы не владеем этой памятью, освобождать её не нужно

        createTextureImageView(VK_IMAGE_VIEW_TYPE_2D);
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
            mTextureSampler = mDevice.getLinearClampSampler();
            mDescriptor.sampler = mTextureSampler;
            mDescriptor.imageView = mTextureImageView;
            mDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    BurnhopeTexture::BurnhopeTexture(
        BurnhopeDevice &device,
        VkFormat format,
        VkExtent3D extent,
        VkImageUsageFlags usage,
        VkSampleCountFlagBits sampleCount,
        uint32_t arrayLayers)
        : mDevice{device}
    {
        mFormat = format;
        mExtent = extent;
        mLayerCount = arrayLayers;
        VkImageAspectFlags aspectMask =
            (format == VK_FORMAT_D32_SFLOAT ||
             format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
             format == VK_FORMAT_D24_UNORM_S8_UINT)
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_COLOR_BIT;
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = arrayLayers;
        imageInfo.samples = sampleCount;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        device.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   mTextureImage, mTextureImageMemory);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mTextureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = arrayLayers;
        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &mTextureImageView) != VK_SUCCESS)
            throw std::runtime_error("failed to create array texture image view!");
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            if (format == VK_FORMAT_R8_UINT || format == VK_FORMAT_R16_UINT || format == VK_FORMAT_R32_UINT) {
                mTextureSampler = mDevice.getNearestClampSampler();
            } else {
                mTextureSampler = mDevice.getLinearClampSampler();
            }
            mDescriptor.sampler = mTextureSampler;
            mDescriptor.imageView = mTextureImageView;
            mDescriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }
    BurnhopeTexture::BurnhopeTexture(
        BurnhopeDevice &device,
        VkFormat format,
        VkExtent3D extent,
        VkImageUsageFlags usage,
        VkSampleCountFlagBits sampleCount)
        : mDevice{device}
    {
        VkImageAspectFlags aspectMask = 0;
        VkImageLayout imageLayout;
        mFormat = format;
        mExtent = extent;
        if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT)
        {
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        }
        else
        {
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        }
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = extent;
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = sampleCount;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = usage;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        device.createImageWithInfo(
            imageInfo,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mTextureImage,
            mTextureImageMemory);
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange = {};
        viewInfo.subresourceRange.aspectMask = aspectMask;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        viewInfo.image = mTextureImage;
        if (vkCreateImageView(device.device(), &viewInfo, nullptr, &mTextureImageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture image view!");
        }
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            if (format == VK_FORMAT_R8_UINT || format == VK_FORMAT_R16_UINT || format == VK_FORMAT_R32_UINT) {
                mTextureSampler = mDevice.getNearestClampSampler();
            } else {
                mTextureSampler = mDevice.getLinearClampSampler();
            }
            VkImageLayout samplerImageLayout = imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                                   ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                                   : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            mDescriptor.sampler = mTextureSampler;
            mDescriptor.imageView = mTextureImageView;
            mDescriptor.imageLayout = samplerImageLayout;
        }
    }
    BurnhopeTexture::~BurnhopeTexture()
    {
        // vkDestroySampler(mDevice.device(), mTextureSampler, nullptr); // Теперь самплеры глобальные!
        vkDestroyImageView(mDevice.device(), mTextureImageView, nullptr);
          if (mTextureImageMemory != VK_NULL_HANDLE) {
            vmaDestroyImage(mDevice.getAllocator(), mTextureImage, mTextureImageMemory);
        } else if (mTextureImage != VK_NULL_HANDLE) {
            vkDestroyImage(mDevice.device(), mTextureImage, nullptr);
        }
    }
    std::shared_ptr<BurnhopeTexture> BurnhopeTexture::createTextureFromFile(
        BurnhopeDevice &device, const std::string &filepath)
    {
        if (auto tex = textureCache[filepath].lock()) {
            return tex;
        }
        try {
            auto tex = std::make_shared<BurnhopeTexture>(device, filepath, true);
            textureCache[filepath] = tex;
            return tex;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to load texture: " << filepath << " | " << e.what() << "\n";
            return nullptr;
        }
    }
    std::shared_ptr<BurnhopeTexture> BurnhopeTexture::createDataTextureFromFile(
        BurnhopeDevice &device, const std::string &filepath)
    {
        if (auto tex = textureCache[filepath].lock()) {
            return tex;
        }
        try {
            auto tex = std::make_shared<BurnhopeTexture>(device, filepath, false);
            textureCache[filepath] = tex;
            return tex;
        } catch (const std::exception& e) {
            std::cerr << "[ERROR] Failed to load data texture: " << filepath << " | " << e.what() << "\n";
            return nullptr;
        }
    }
    void BurnhopeTexture::updateDescriptor()
    {
        mDescriptor.sampler = mTextureSampler;
        mDescriptor.imageView = mTextureImageView;
        mDescriptor.imageLayout = mTextureLayout;
    }
    void BurnhopeTexture::createTextureImage(const std::string &filepath, bool isSRGB)
    {
        if (filepath.ends_with(".bhtex"))
        {
            if (!loadFromBHTex(filepath))
            {
                throw std::runtime_error("Failed to load .bhtex: " + filepath);
            }
            return;
        }
        std::string cachePath = filepath.substr(0, filepath.find_last_of('.')) + ".bhtex";
        if (std::filesystem::exists(cachePath))
        {
            std::cout << "[CACHE] Быстрая загрузка: " << cachePath << "\n";
            if (loadFromBHTex(cachePath))
            {
                return;
            }
            std::cout << "[CACHE] Кэш поврежден или устарел, пересоздаю...\n";
        }
        std::cout << "[BUILD] Создаю кэш .bhtex для: " << filepath << "\n";
        generateAndCacheBHTex(filepath, cachePath, isSRGB);
        if (loadFromBHTex(cachePath))
        {
            return;
        }
        int texWidth, texHeight, texChannels;
        stbi_uc *pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        VkDeviceSize imageSize = texWidth * texHeight * 4;
        if (!pixels)
        {
            throw std::runtime_error("failed to load texture image: " + filepath);
        }
        mMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        mFormat = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        mExtent = {static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1};
        
        BurnhopeBuffer stagingBuffer{
            mDevice,
            1,
            static_cast<uint32_t>(imageSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        stagingBuffer.map();
        stagingBuffer.writeToBuffer(pixels);
        stbi_image_free(pixels);
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = mExtent;
        imageInfo.mipLevels = mMipLevels;
        imageInfo.arrayLayers = mLayerCount;
        imageInfo.format = mFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        mDevice.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mTextureImage, mTextureImageMemory);
        transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        mDevice.copyBufferToImage(stagingBuffer.getBuffer(), mTextureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), mLayerCount);
        generateMipmaps(mTextureImage, mFormat, texWidth, texHeight, mMipLevels);
        mTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    void BurnhopeTexture::createTextureImageView(VkImageViewType viewType)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = mTextureImage;
        viewInfo.viewType = viewType;
        viewInfo.format = mFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mMipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = mLayerCount;
        if (vkCreateImageView(mDevice.device(), &viewInfo, nullptr, &mTextureImageView) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to create texture image view!");
        }
    }
    void BurnhopeTexture::createTextureSampler()
    {
        // Все обычные 2D PBR-текстуры с мипмапами используют Linear Repeat
        mTextureSampler = mDevice.getLinearRepeatSampler();
    }
    void BurnhopeTexture::transitionLayout(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = mTextureImage;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mMipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = mLayerCount;
        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if (mFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || mFormat == VK_FORMAT_D24_UNORM_S8_UINT)
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }
        VkPipelineStageFlags2 sourceStage;
        VkPipelineStageFlags2 destinationStage;
        if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }
        else if (
            oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        }
        else if (
            oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        }
        else if (
            oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
            newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask =
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (
            oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
            newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstAccessMask =
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        }
        else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        }
        else
        {
            throw std::invalid_argument("unsupported layout transition!");
        }
        
        VkImageMemoryBarrier2 barrier2{};
        barrier2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier2.oldLayout = barrier.oldLayout;
        barrier2.newLayout = barrier.newLayout;
        barrier2.srcQueueFamilyIndex = barrier.srcQueueFamilyIndex;
        barrier2.dstQueueFamilyIndex = barrier.dstQueueFamilyIndex;
        barrier2.image = barrier.image;
        barrier2.subresourceRange = barrier.subresourceRange;
        barrier2.srcStageMask = sourceStage;
        barrier2.srcAccessMask = barrier.srcAccessMask;
        barrier2.dstStageMask = destinationStage;
        barrier2.dstAccessMask = barrier.dstAccessMask;
        
        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &barrier2;
        
        vkCmdPipelineBarrier2(commandBuffer, &depInfo);
        mDevice.endSingleTimeCommands(commandBuffer);
    }
    void BurnhopeTexture::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels)
    {
        VkFormatProperties formatProperties;
        vkGetPhysicalDeviceFormatProperties(mDevice.getPhysicalDevice(), imageFormat, &formatProperties);
        if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT))
        {
            throw std::runtime_error("Texture image format does not support linear blitting!");
        }
        VkCommandBuffer commandBuffer = mDevice.beginSingleTimeCommands();
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.image = image;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = mLayerCount;
        barrier.subresourceRange.levelCount = 1;
        int32_t mipWidth = texWidth;
        int32_t mipHeight = texHeight;
        for (uint32_t i = 1; i < mipLevels; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            
            VkDependencyInfo depInfo{};
            depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo.imageMemoryBarrierCount = 1;
            depInfo.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &depInfo);
            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = mLayerCount;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1};
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = mLayerCount;
            vkCmdBlitImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit,
                           VK_FILTER_LINEAR);
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
            
            VkDependencyInfo depInfo2{};
            depInfo2.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            depInfo2.imageMemoryBarrierCount = 1;
            depInfo2.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(commandBuffer, &depInfo2);
            if (mipWidth > 1)
                mipWidth /= 2;
            if (mipHeight > 1)
                mipHeight /= 2;
        }
        barrier.subresourceRange.baseMipLevel = mipLevels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT;
        
        VkDependencyInfo depInfo3{};
        depInfo3.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo3.imageMemoryBarrierCount = 1;
        depInfo3.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(commandBuffer, &depInfo3);
        mDevice.endSingleTimeCommands(commandBuffer);
    }
    bool BurnhopeTexture::loadFromBHTex(const std::string &filepath)
    {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open())
            return false;
        BHTexHeader header;
        if (!file.read(reinterpret_cast<char *>(&header), sizeof(BHTexHeader))) return false;
        if (strncmp(header.magic, "BHTX", 4) != 0) return false;
        if (header.mipCount == 0 || header.mipCount > 16) return false;
        mMipLevels = header.mipCount;
        mExtent = {header.width, header.height, 1};
        if (header.format == 0)
            mFormat = header.isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        else if (header.format == 1)
            mFormat = header.isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
        else if (header.format == 2)
            mFormat = VK_FORMAT_BC5_UNORM_BLOCK;
        else if (header.format == 3)
            mFormat = header.isSRGB ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
        else if (header.format == 4)
            mFormat = VK_FORMAT_BC6H_UFLOAT_BLOCK;
        else
            mFormat = header.isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        bool isCompressed = (header.format <= 4);
        std::vector<char> allMipData;
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        uint32_t currentW = header.width;
        uint32_t currentH = header.height;
        for (uint32_t i = 0; i < mMipLevels; ++i)
        {
            uint32_t alignW = isCompressed ? ((currentW + 3) & ~3) : 0;
            uint32_t alignH = isCompressed ? ((currentH + 3) & ~3) : 0;


            uint32_t dataSize;
            if (!file.read(reinterpret_cast<char *>(&dataSize), sizeof(uint32_t))) return false;
            if (dataSize == 0 || dataSize > 1024 * 1024 * 512) return false;

            size_t currentOffset = allMipData.size();
            try { allMipData.resize(currentOffset + dataSize); } catch(...) { return false; }
            if (!file.read(allMipData.data() + currentOffset, dataSize)) return false;
            VkBufferImageCopy region{};
            region.bufferOffset = currentOffset;
            region.bufferRowLength = alignW; // Обязательно указываем выровненный размер блоков для Vulkan
            region.bufferImageHeight = alignH;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = i;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {currentW, currentH, 1};
            bufferCopyRegions.push_back(region);
            currentW = std::max(1u, currentW / 2);
            currentH = std::max(1u, currentH / 2);
        }
        file.close();
        
        if (allMipData.empty()) {
            return false;
        }

        BurnhopeBuffer stagingBuffer{
            mDevice,
            1,
            static_cast<uint32_t>(allMipData.size()),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        };
        stagingBuffer.map();
        stagingBuffer.writeToBuffer(allMipData.data());

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = mExtent;
        imageInfo.mipLevels = mMipLevels;
        imageInfo.arrayLayers = mLayerCount;
        imageInfo.format = mFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        mDevice.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mTextureImage, mTextureImageMemory);
        transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkCommandBuffer commandBuffer = mDevice.beginSingleTimeCommands();
        vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.getBuffer(), mTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
        mDevice.endSingleTimeCommands(commandBuffer);
        transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        return true;
    }
    void BurnhopeTexture::generateAndCacheBHTex(const std::string &srcPath, const std::string &cachePath, bool isSRGB)
    {
        int texWidth, texHeight, texChannels;
        stbi_uc *pixels = stbi_load(srcPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
        if (!pixels)
            throw std::runtime_error("Failed to load source image: " + srcPath);
        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
        BHTexHeader header{};
        header.width = texWidth;
        header.height = texHeight;
        header.mipCount = mipLevels;
        header.isSRGB = isSRGB;
        header.format = 5;
        memcpy(header.magic, "BHTX", 4);
        std::ofstream outFile(cachePath, std::ios::binary);
        outFile.write(reinterpret_cast<const char *>(&header), sizeof(BHTexHeader));
        int currentW = texWidth;
        int currentH = texHeight;
        std::vector<stbi_uc> currentMip(pixels, pixels + (texWidth * texHeight * 4));
        for (uint32_t i = 0; i < mipLevels; ++i)
        {
            uint32_t dataSize = currentW * currentH * 4;
            outFile.write(reinterpret_cast<char *>(&dataSize), sizeof(uint32_t));
            outFile.write(reinterpret_cast<char *>(currentMip.data()), dataSize);
            if (i < mipLevels - 1)
            {
                int nextW = std::max(1, currentW / 2);
                int nextH = std::max(1, currentH / 2);
                std::vector<stbi_uc> nextMip(nextW * nextH * 4);
                if (isSRGB)
                {
                    stbir_resize_uint8_srgb(currentMip.data(), currentW, currentH, 0,
                                            nextMip.data(), nextW, nextH, 0, STBIR_RGBA);
                }
                else
                {
                    stbir_resize_uint8_linear(currentMip.data(), currentW, currentH, 0,
                                              nextMip.data(), nextW, nextH, 0, STBIR_RGBA);
                }
                currentMip = std::move(nextMip);
                currentW = nextW;
                currentH = nextH;
            }
        }
        outFile.close();
        stbi_image_free(pixels);
        std::cout << "[DONE] Кэш .bhtex создан для: " << srcPath << "\n";
    }

    static std::vector<stbi_uc> loadChannel(const std::string& path, int& w, int& h) {
        if (path.empty() || !std::filesystem::exists(path)) { w = h = 0; return {}; }
        int c;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!pixels) { w = h = 0; return {}; }
        std::vector<stbi_uc> res(pixels, pixels + w * h * 4);
        stbi_image_free(pixels);
        return res;
    }

     static std::vector<stbi_uc> resizeChannel(const std::vector<stbi_uc>& src, int inW, int inH, int outW, int outH, bool isSRGB) {
        if (src.empty() || (inW == outW && inH == outH)) return src;
        std::vector<stbi_uc> resized(outW * outH * 4);
        if (isSRGB) {
            // Для Albedo (цвета) альфа-смешивание это нормально
            stbir_resize_uint8_srgb(src.data(), inW, inH, 0, resized.data(), outW, outH, 0, STBIR_RGBA);
        } else {
            // ДЛЯ ДАННЫХ (Normal, ORMX) ОТКЛЮЧАЕМ АЛЬФА-СМЕШИВАНИЕ!
            stbir_resize_uint8_linear(src.data(), inW, inH, 0, resized.data(), outW, outH, 0, STBIR_4CHANNEL);
        }
        return resized;
    }
    // Утилита для конвертации float32 -> float16 (half)
    static inline uint16_t float_to_half(float f) {
        uint32_t x = *(uint32_t*)&f;
        uint32_t sign = (uint16_t)(x >> 31);
        uint32_t mantissa = x & 0x007fffff;
        uint32_t exp = (x & 0x7f800000) >> 23;
        if (exp == 0) return (sign << 15);
        if (exp == 255) return (sign << 15) | 0x7c00 | (mantissa ? 1 : 0);
        int e = exp - 127 + 15;
        if (e >= 31) return (sign << 15) | 0x7c00;
        if (e <= 0) return (sign << 15);
        return (sign << 15) | (e << 10) | (mantissa >> 13);
    }

    static void writeBHTexPacked(const std::string& outPath, std::vector<stbi_uc>& packedData, int w, int h, BHTexHeader header) {
        uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(w, h)))) + 1;
        header.width = w; header.height = h; header.mipCount = mipLevels;
        memcpy(header.magic, "BHTX", 4);

        std::ofstream outFile(outPath, std::ios::binary);
        outFile.write(reinterpret_cast<const char*>(&header), sizeof(BHTexHeader));

        int currentW = w; int currentH = h;
        std::vector<stbi_uc> currentMip = packedData;

        if (header.isNormalMap) {
            for (size_t k = 0; k < w * h; k++) {
                currentMip[k * 4 + 3] = 0; 
            }
        }

        for (uint32_t i = 0; i < mipLevels; ++i) {
            int alignW = (currentW + 3) & ~3;
            int alignH = (currentH + 3) & ~3;
            int blocksX = alignW / 4;
            int blocksY = alignH / 4;
            int totalBlocks = blocksX * blocksY;

            // Выравнивание и паддинг краев (критично для ISPC и BC форматов)
            std::vector<stbi_uc> paddedMip(alignW * alignH * 4, 0);
            for (int y = 0; y < alignH; y++) {
                for (int x = 0; x < alignW; x++) {
                    int srcX = std::min(x, currentW - 1);
                    int srcY = std::min(y, currentH - 1);
                    for (int c = 0; c < 4; c++) {
                        paddedMip[(y * alignW + x) * 4 + c] = currentMip[(srcY * currentW + srcX) * 4 + c];
                    }
                }
            }
            
            rgba_surface surface;
            surface.ptr = paddedMip.data();
            surface.width = alignW;
            surface.height = alignH;
            surface.stride = alignW * 4;

            if (header.packType >= 1 && header.packType <= 4) {
                std::vector<uint8_t> compressedData(totalBlocks * 16);

                if (header.packType == 1 || header.packType == 2) {
                    bc7_enc_settings settings;
                    GetProfile_alpha_fast(&settings); 
                    CompressBlocksBC7(&surface, compressedData.data(), &settings);
                } else if (header.packType == 3) {
                    CompressBlocksBC5(&surface, compressedData.data());
                } else if (header.packType == 4) {
                    std::vector<uint16_t> halfData(alignW * alignH * 4);
                    for (size_t k = 0; k < alignW * alignH; k++) {
                        float r = std::pow(paddedMip[k * 4 + 0] / 255.0f, 2.2f); 
                        float g = std::pow(paddedMip[k * 4 + 1] / 255.0f, 2.2f);
                        float b = std::pow(paddedMip[k * 4 + 2] / 255.0f, 2.2f);
                        halfData[k * 4 + 0] = float_to_half(r);
                        halfData[k * 4 + 1] = float_to_half(g);
                        halfData[k * 4 + 2] = float_to_half(b);
                        halfData[k * 4 + 3] = float_to_half(1.0f);
                    }
                    surface.ptr = (uint8_t*)halfData.data();
                    surface.stride = alignW * 4 * 2; 
                    
                    bc6h_enc_settings settings;
                    GetProfile_bc6h_basic(&settings);
                    CompressBlocksBC6H(&surface, compressedData.data(), &settings);
                }

                uint32_t dataSize = compressedData.size();
                outFile.write(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));
                outFile.write(reinterpret_cast<char*>(compressedData.data()), dataSize);
            } else {
                // RAW (Сырые данные без сжатия)
                uint32_t dataSize = currentW * currentH * 4;
                outFile.write(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));
                outFile.write(reinterpret_cast<char*>(currentMip.data()), dataSize);
            }

            if (i < mipLevels - 1) {
                int nextW = std::max(1, currentW / 2);
                int nextH = std::max(1, currentH / 2);
                std::vector<stbi_uc> nextMip(nextW * nextH * 4);
                if (header.isSRGB) {
                    // Albedo
                    stbir_resize_uint8_srgb(currentMip.data(), currentW, currentH, 0, nextMip.data(), nextW, nextH, 0, STBIR_RGBA);
                } else {
                    // Normal, ORMX, Emissive
                    stbir_resize_uint8_linear(currentMip.data(), currentW, currentH, 0, nextMip.data(), nextW, nextH, 0, STBIR_4CHANNEL);
                }

                if (header.isNormalMap) {
                    for (int y = 0; y < nextH; y++) {
                        for (int x = 0; x < nextW; x++) {
                            int idx = (y * nextW + x) * 4;
                            float nx = (nextMip[idx + 0] / 255.0f) * 2.0f - 1.0f;
                            float ny = (nextMip[idx + 1] / 255.0f) * 2.0f - 1.0f;
                            float nz = (nextMip[idx + 2] / 255.0f) * 2.0f - 1.0f;
                            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
                            float variance = std::clamp(1.0f - len, 0.0f, 1.0f);
                            
                            int p_x = std::min(x * 2, currentW - 1);
                            int p_y = std::min(y * 2, currentH - 1);
                            float prevVar = currentMip[(p_y * currentW + p_x) * 4 + 3] / 255.0f;
                            float totalVar = std::clamp(prevVar + variance, 0.0f, 1.0f);
                            nextMip[idx + 3] = static_cast<stbi_uc>(totalVar * 255.0f);

                            if (len > 0.0001f) {
                                nextMip[idx + 0] = static_cast<stbi_uc>(((nx / len) * 0.5f + 0.5f) * 255.0f);
                                nextMip[idx + 1] = static_cast<stbi_uc>(((ny / len) * 0.5f + 0.5f) * 255.0f);
                                nextMip[idx + 2] = static_cast<stbi_uc>(((nz / len) * 0.5f + 0.5f) * 255.0f);
                            }
                        }
                    }
                }

                currentMip = std::move(nextMip);
                currentW = nextW; currentH = nextH;
            }
        }
        outFile.close();
        std::cout << "[PACKER] Успешно запаковано и сгенерированы Lanczos-мипмапы: " << outPath << "\n";
    }

    void BurnhopeTexture::packORMX(const std::string& ao, const std::string& rough, const std::string& metal, const std::string& height, const std::string& outPath) {
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0, w4 = 0, h4 = 0;
        auto d1 = loadChannel(ao, w1, h1);
        auto d2 = loadChannel(rough, w2, h2);
        auto d3 = loadChannel(metal, w3, h3);
        auto d4 = loadChannel(height, w4, h4);

        int maxW = std::max({w1, w2, w3, w4});
        int maxH = std::max({h1, h2, h3, h4});

        if (maxW == 0 || maxH == 0) return;

        if (maxW > 2048 || maxH > 2048) {
            float aspect = (float)maxW / maxH;
            if (maxW > maxH) { maxW = 2048; maxH = 2048 / aspect; }
            else { maxH = 2048; maxW = 2048 * aspect; }
        }

        d1 = resizeChannel(d1, w1, h1, maxW, maxH, false);
        d2 = resizeChannel(d2, w2, h2, maxW, maxH, false);
        d3 = resizeChannel(d3, w3, h3, maxW, maxH, false);
        d4 = resizeChannel(d4, w4, h4, maxW, maxH, false);

        std::vector<stbi_uc> packed(maxW * maxH * 4, 255);
        for (size_t i = 0; i < maxW * maxH; ++i) {
            packed[i * 4 + 0] = d1.empty() ? 255 : d1[i * 4 + 0]; // AO (1.0)
            packed[i * 4 + 1] = d2.empty() ? 255 : d2[i * 4 + 0]; // Roughness (1.0)
            packed[i * 4 + 2] = d3.empty() ? 255 : d3[i * 4 + 0]; // Metallic (1.0)
            packed[i * 4 + 3] = d4.empty() ? 0   : d4[i * 4 + 0]; // Height (0.0)
        }

        BHTexHeader hdr{}; hdr.format = 3; hdr.isSRGB = false; hdr.hasAlpha = true; hdr.packType = 2; // ORMX -> BC7 UNORM
        strncpy(hdr.srcPath1, ao.c_str(), 255); strncpy(hdr.srcPath2, rough.c_str(), 255);
        strncpy(hdr.srcPath3, metal.c_str(), 255); strncpy(hdr.srcPath4, height.c_str(), 255);
        writeBHTexPacked(outPath, packed, maxW, maxH, hdr);
    }

    void BurnhopeTexture::packAlbedoAlpha(const std::string& albedo, const std::string& alpha, const std::string& outPath) {
        int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
        auto aData = loadChannel(albedo, w1, h1);
        auto alphaData = loadChannel(alpha, w2, h2);
        
        int maxW = std::max(w1, w2);
        int maxH = std::max(h1, h2);
        if (maxW == 0 || maxH == 0) return;

        if (maxW > 2048 || maxH > 2048) {
            float aspect = (float)maxW / maxH;
            if (maxW > maxH) { maxW = 2048; maxH = 2048 / aspect; }
            else { maxH = 2048; maxW = 2048 * aspect; }
        }

        aData = resizeChannel(aData, w1, h1, maxW, maxH, true);
        alphaData = resizeChannel(alphaData, w2, h2, maxW, maxH, false);

        if (aData.empty() && !alphaData.empty()) aData.assign(maxW * maxH * 4, 255);

        for (size_t i = 0; i < maxW * maxH; ++i) {
            aData[i * 4 + 3] = alphaData.empty() ? 255 : alphaData[i * 4 + 0];
        }
        BHTexHeader hdr{}; hdr.format = 3; hdr.isSRGB = true; hdr.hasAlpha = !alphaData.empty(); hdr.packType = 1; // RGBA -> BC7 SRGB
        strncpy(hdr.srcPath1, albedo.c_str(), 255); strncpy(hdr.srcPath2, alpha.c_str(), 255);
        writeBHTexPacked(outPath, aData, maxW, maxH, hdr);
    }

    void BurnhopeTexture::packNormal(const std::string& normal, const std::string& outPath) {
        int w = 0, h = 0; auto nData = loadChannel(normal, w, h);
        if (nData.empty()) return;

        int outW = w, outH = h;
        if (w > 2048 || h > 2048) {
            float aspect = (float)w / h;
            if (w > h) { outW = 2048; outH = 2048 / aspect; }
            else { outH = 2048; outW = 2048 * aspect; }
            nData = resizeChannel(nData, w, h, outW, outH, false);
        }

        BHTexHeader hdr{}; hdr.format = 3; hdr.isSRGB = false; hdr.hasAlpha = true; hdr.packType = 1; // Normal -> BC7 UNORM
        hdr.isNormalMap = true;
        strncpy(hdr.srcPath1, normal.c_str(), 255);
        writeBHTexPacked(outPath, nData, outW, outH, hdr);
    }

    void BurnhopeTexture::packEmissive(const std::string& emissive, const std::string& outPath) {
        int w = 0, h = 0; auto eData = loadChannel(emissive, w, h);
        if (eData.empty()) return;
        
        int outW = w, outH = h;
        if (w > 2048 || h > 2048) {
            float aspect = (float)w / h;
            if (w > h) { outW = 2048; outH = 2048 / aspect; }
            else { outH = 2048; outW = 2048 * aspect; }
            eData = resizeChannel(eData, w, h, outW, outH, true);
        }

        BHTexHeader hdr{}; hdr.format = 4; hdr.isSRGB = false; hdr.hasAlpha = false; hdr.packType = 4; // Emissive -> BC6H UFLOAT (hdr=linear)
        strncpy(hdr.srcPath1, emissive.c_str(), 255);
        writeBHTexPacked(outPath, eData, outW, outH, hdr);
    }

    void BurnhopeTexture::rebuildFromHeader(const std::string& bhtexPath) {
        std::ifstream file(bhtexPath, std::ios::binary);
        if (!file.is_open()) return;
        BHTexHeader hdr; file.read(reinterpret_cast<char*>(&hdr), sizeof(BHTexHeader)); file.close();
        if (hdr.packType == 1) packAlbedoAlpha(hdr.srcPath1, hdr.srcPath2, bhtexPath);
        else if (hdr.packType == 2) packORMX(hdr.srcPath1, hdr.srcPath2, hdr.srcPath3, hdr.srcPath4, bhtexPath);
        else if (hdr.packType == 3) packNormal(hdr.srcPath1, bhtexPath);
        else if (hdr.packType == 4) packEmissive(hdr.srcPath1, bhtexPath);
    }
}
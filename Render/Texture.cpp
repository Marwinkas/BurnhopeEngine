#include "Texture.hpp"
#include <stb_image.h>

#include <stb/stb_image_resize2.h>

#include <cstring>
#include <cmath>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <filesystem>
namespace burnhope {
BurnhopeTexture::BurnhopeTexture(BurnhopeDevice &device, const std::string &textureFilepath, bool isSRGB) : mDevice{device} {
  createTextureImage(textureFilepath, isSRGB);
  createTextureImageView(VK_IMAGE_VIEW_TYPE_2D);
  createTextureSampler();
  updateDescriptor();
}
BurnhopeTexture::BurnhopeTexture(
    BurnhopeDevice& device,
    VkFormat format,
    VkExtent3D extent,
    VkImageUsageFlags usage,
    VkSampleCountFlagBits sampleCount,
    uint32_t arrayLayers)
    : mDevice{ device }
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

    
    if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE; 
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &mTextureSampler) != VK_SUCCESS)
            throw std::runtime_error("failed to create array texture sampler!");

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
    : mDevice{device} {
  VkImageAspectFlags aspectMask = 0;
  VkImageLayout imageLayout;

  mFormat = format;
  mExtent = extent;
  
  if (format == VK_FORMAT_D32_SFLOAT || format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT) {
      aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
      
      imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  }
  else {
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
  if (vkCreateImageView(device.device(), &viewInfo, nullptr, &mTextureImageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }

  
  if (usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
    
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    samplerInfo.addressModeV = samplerInfo.addressModeU;
    samplerInfo.addressModeW = samplerInfo.addressModeU;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

    if (vkCreateSampler(device.device(), &samplerInfo, nullptr, &mTextureSampler) != VK_SUCCESS) {
      throw std::runtime_error("failed to create sampler!");
    }

    VkImageLayout samplerImageLayout = imageLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                                           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                           : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    mDescriptor.sampler = mTextureSampler;
    mDescriptor.imageView = mTextureImageView;
    mDescriptor.imageLayout = samplerImageLayout;
  }
}

BurnhopeTexture::~BurnhopeTexture() {
  vkDestroySampler(mDevice.device(), mTextureSampler, nullptr);
  vkDestroyImageView(mDevice.device(), mTextureImageView, nullptr);
  vkDestroyImage(mDevice.device(), mTextureImage, nullptr);
  vkFreeMemory(mDevice.device(), mTextureImageMemory, nullptr);
}

std::unique_ptr<BurnhopeTexture> BurnhopeTexture::createTextureFromFile(
    BurnhopeDevice &device, const std::string &filepath) {
  return std::make_unique<BurnhopeTexture>(device, filepath, true);
}


std::unique_ptr<BurnhopeTexture> BurnhopeTexture::createDataTextureFromFile(
    BurnhopeDevice &device, const std::string &filepath) {
  return std::make_unique<BurnhopeTexture>(device, filepath, false); 
}

void BurnhopeTexture::updateDescriptor() {
  mDescriptor.sampler = mTextureSampler;
  mDescriptor.imageView = mTextureImageView;
  mDescriptor.imageLayout = mTextureLayout;
}

void BurnhopeTexture::createTextureImage(const std::string& filepath, bool isSRGB) {

    
    if (filepath.ends_with(".bhtex")) {
        if (!loadFromBHTex(filepath)) {
            throw std::runtime_error("Failed to load .bhtex: " + filepath);
        }
        return;
    }

    
    std::string cachePath = filepath.substr(0, filepath.find_last_of('.')) + ".bhtex";
    
    if (std::filesystem::exists(cachePath)) {
        
        std::cout << "[CACHE] Быстрая загрузка: " << cachePath << "\n";
        if (loadFromBHTex(cachePath)) {
            return;
        }
    }

    
    std::cout << "[BUILD] Создаю кэш .bhtex для: " << filepath << "\n";
    generateAndCacheBHTex(filepath, cachePath, isSRGB);
    
    
    if (!loadFromBHTex(cachePath)) {
        throw std::runtime_error("Failed to load generated .bhtex!");
    }
    
    
    
    if (filepath.ends_with(".bhtex")) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open .bhtex file: " + filepath);
        }

        BHTexHeader header;
        file.read(reinterpret_cast<char*>(&header), sizeof(BHTexHeader));

        mMipLevels = header.mipCount;
        mExtent = { header.width, header.height, 1 };

        
        if (header.format == 1) { 
            mFormat = header.isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
        }
        else if (header.format == 2) { 
            mFormat = VK_FORMAT_BC5_UNORM_BLOCK;
        }
        else { 
            mFormat = header.isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
        }

        
        std::vector<char> allMipData;
        std::vector<VkBufferImageCopy> bufferCopyRegions;

        uint32_t currentW = header.width;
        uint32_t currentH = header.height;

        for (uint32_t i = 0; i < mMipLevels; ++i) {
            uint32_t dataSize;
            file.read(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));

            size_t currentOffset = allMipData.size();
            allMipData.resize(currentOffset + dataSize);
            file.read(allMipData.data() + currentOffset, dataSize);

            
            VkBufferImageCopy region{};
            region.bufferOffset = currentOffset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = i;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { currentW, currentH, 1 };

            bufferCopyRegions.push_back(region);

            currentW = std::max(1u, currentW / 2);
            currentH = std::max(1u, currentH / 2);
        }
        file.close();

        
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        mDevice.createBuffer(
            allMipData.size(),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory);

        void* data;
        vkMapMemory(mDevice.device(), stagingBufferMemory, 0, allMipData.size(), 0, &data);
        memcpy(data, allMipData.data(), allMipData.size());
        vkUnmapMemory(mDevice.device(), stagingBufferMemory);

        
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
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        mDevice.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, mTextureImage, mTextureImageMemory);

        
        transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        
        VkCommandBuffer commandBuffer = mDevice.beginSingleTimeCommands();
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            mTextureImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<uint32_t>(bufferCopyRegions.size()),
            bufferCopyRegions.data());
        mDevice.endSingleTimeCommands(commandBuffer);

        
        transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        mTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        
        vkDestroyBuffer(mDevice.device(), stagingBuffer, nullptr);
        vkFreeMemory(mDevice.device(), stagingBufferMemory, nullptr);
        return;
    }

    
    
    
    
    
    
    int texWidth, texHeight, texChannels;
    stbi_uc* pixels = stbi_load(filepath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    VkDeviceSize imageSize = texWidth * texHeight * 4;
    if (!pixels) {
        throw std::runtime_error("failed to load texture image: " + filepath);
    }

    mMipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;
    mFormat = isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    mExtent = { static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), 1 };

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    mDevice.createBuffer(
        imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(mDevice.device(), stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(mDevice.device(), stagingBufferMemory);
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

    
    mDevice.copyBufferToImage(stagingBuffer, mTextureImage, static_cast<uint32_t>(texWidth), static_cast<uint32_t>(texHeight), mLayerCount);

    
    generateMipmaps(mTextureImage, mFormat, texWidth, texHeight, mMipLevels);
    mTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkDestroyBuffer(mDevice.device(), stagingBuffer, nullptr);
    vkFreeMemory(mDevice.device(), stagingBufferMemory, nullptr);
}

void BurnhopeTexture::createTextureImageView(VkImageViewType viewType) {
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

  if (vkCreateImageView(mDevice.device(), &viewInfo, nullptr, &mTextureImageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }
}

void BurnhopeTexture::createTextureSampler() {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;

  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = 16.0f;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;

  
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerInfo.mipLodBias = 0.0f;
  samplerInfo.minLod = 0.0f;
  samplerInfo.maxLod = static_cast<float>(mMipLevels);

  if (vkCreateSampler(mDevice.device(), &samplerInfo, nullptr, &mTextureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}

void BurnhopeTexture::transitionLayout(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout) {
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

  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (mFormat == VK_FORMAT_D32_SFLOAT_S8_UINT || mFormat == VK_FORMAT_D24_UNORM_S8_UINT) {
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
      newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (
      oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
      newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    
    
    
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

    sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } 
  else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_GENERAL) {
      barrier.srcAccessMask = 0;
      barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
      sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
      destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  }
  else {
    throw std::invalid_argument("unsupported layout transition!");
  }
  vkCmdPipelineBarrier(
      commandBuffer,
      sourceStage, destinationStage,
      0,
      0, nullptr,
      0, nullptr,
      1, &barrier);
  mDevice.endSingleTimeCommands(commandBuffer);
}
void BurnhopeTexture::generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(mDevice.getPhysicalDevice(), imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("Texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = mDevice.beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = mLayerCount; 
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = mLayerCount;

        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
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
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr, 0, nullptr, 1, &barrier);

        
        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr, 0, nullptr, 1, &barrier);

    mDevice.endSingleTimeCommands(commandBuffer);
}
bool BurnhopeTexture::loadFromBHTex(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    BHTexHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(BHTexHeader));

    mMipLevels = header.mipCount;
    mExtent = { header.width, header.height, 1 };

    
    if (header.format == 0) mFormat = header.isSRGB ? VK_FORMAT_BC1_RGB_SRGB_BLOCK : VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    else if (header.format == 1) mFormat = header.isSRGB ? VK_FORMAT_BC3_SRGB_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    else if (header.format == 2) mFormat = VK_FORMAT_BC5_UNORM_BLOCK;
    else if (header.format == 3) mFormat = header.isSRGB ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
    else mFormat = header.isSRGB ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM; 

    
    std::vector<char> allMipData;
    std::vector<VkBufferImageCopy> bufferCopyRegions;

    uint32_t currentW = header.width;
    uint32_t currentH = header.height;

    for (uint32_t i = 0; i < mMipLevels; ++i) {
        uint32_t dataSize;
        file.read(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));

        size_t currentOffset = allMipData.size();
        allMipData.resize(currentOffset + dataSize);
        file.read(allMipData.data() + currentOffset, dataSize);

        VkBufferImageCopy region{};
        region.bufferOffset = currentOffset;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { currentW, currentH, 1 };
        bufferCopyRegions.push_back(region);

        currentW = std::max(1u, currentW / 2);
        currentH = std::max(1u, currentH / 2);
    }
    file.close();

    
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    mDevice.createBuffer(allMipData.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer, stagingBufferMemory);

    void* data;
    vkMapMemory(mDevice.device(), stagingBufferMemory, 0, allMipData.size(), 0, &data);
    memcpy(data, allMipData.data(), allMipData.size());
    vkUnmapMemory(mDevice.device(), stagingBufferMemory);

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
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, mTextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
    mDevice.endSingleTimeCommands(commandBuffer);

    transitionLayout(mDevice.beginSingleTimeCommands(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    mTextureLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    vkDestroyBuffer(mDevice.device(), stagingBuffer, nullptr);
    vkFreeMemory(mDevice.device(), stagingBufferMemory, nullptr);
    return true;
}

void BurnhopeTexture::generateAndCacheBHTex(const std::string& srcPath, const std::string& cachePath, bool isSRGB) {
    int texWidth, texHeight, texChannels;
    
    stbi_uc* pixels = stbi_load(srcPath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
    if (!pixels) throw std::runtime_error("Failed to load source image: " + srcPath);

    uint32_t mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1;

    BHTexHeader header{};
    header.width = texWidth;
    header.height = texHeight;
    header.mipCount = mipLevels;
    header.isSRGB = isSRGB;
    header.format = 4; 

    std::ofstream outFile(cachePath, std::ios::binary);
    outFile.write(reinterpret_cast<const char*>(&header), sizeof(BHTexHeader));

    int currentW = texWidth;
    int currentH = texHeight;
    
    
    std::vector<stbi_uc> currentMip(pixels, pixels + (texWidth * texHeight * 4));

    for (uint32_t i = 0; i < mipLevels; ++i) {
        
        uint32_t dataSize = currentW * currentH * 4; 
        outFile.write(reinterpret_cast<char*>(&dataSize), sizeof(uint32_t));
        outFile.write(reinterpret_cast<char*>(currentMip.data()), dataSize);

        
        if (i < mipLevels - 1) {
            int nextW = std::max(1, currentW / 2);
            int nextH = std::max(1, currentH / 2);
            std::vector<stbi_uc> nextMip(nextW * nextH * 4);

            
            if (isSRGB) {
                
                stbir_resize_uint8_srgb(currentMip.data(), currentW, currentH, 0,
                                        nextMip.data(), nextW, nextH, 0, STBIR_RGBA);
            } else {
                
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

}
#include "HiZSystem.hpp"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace burnhope {

struct HiZPush {
    glm::vec2 invSize;
};

HiZSystem::HiZSystem(BurnhopeDevice& device, VkExtent2D extent, BurnhopeDescriptorPool& pool, 
                     VkImageView gDepthView, VkSampler depthSampler)
    : lveDevice(device), globalPool(pool) 
{
    
    setLayout = BurnhopeDescriptorSetLayout::Builder(lveDevice)
        .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT)
        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)
        .build();

    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZPush)};

    auto layout = setLayout->getDescriptorSetLayout();
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &layout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    vkCreatePipelineLayout(lveDevice.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout);

    
    std::ifstream file("shaders/hiz_downsample.comp.spv", std::ios::binary);
    std::vector<char> code((std::istreambuf_iterator<char>(file)), {});
    VkShaderModuleCreateInfo shaderInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr, 0, code.size(), reinterpret_cast<const uint32_t*>(code.data())};
    VkShaderModule shaderModule;
    vkCreateShaderModule(lveDevice.device(), &shaderInfo, nullptr, &shaderModule);

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, shaderModule, "main", nullptr};
    vkCreateComputePipelines(lveDevice.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(lveDevice.device(), shaderModule, nullptr);

    createResources(extent, gDepthView, depthSampler);
}

HiZSystem::~HiZSystem() {
    destroyResources();
    vkDestroyPipeline(lveDevice.device(), pipeline, nullptr);
    vkDestroyPipelineLayout(lveDevice.device(), pipelineLayout, nullptr);
}

void HiZSystem::destroyResources() {
    if (hiZImage == VK_NULL_HANDLE) return;
    
    if (!mipSets.empty()) {
        globalPool.freeDescriptors(mipSets);
        mipSets.clear();
    }
    for (auto view : mipViews) vkDestroyImageView(lveDevice.device(), view, nullptr);
    mipViews.clear();

    vkDestroySampler(lveDevice.device(), hiZSampler, nullptr);
    vkDestroyImageView(lveDevice.device(), fullView, nullptr);
    vkDestroyImage(lveDevice.device(), hiZImage, nullptr);
    vkFreeMemory(lveDevice.device(), hiZMemory, nullptr);
    hiZImage = VK_NULL_HANDLE;
}

void HiZSystem::rebuild(VkExtent2D extent, VkImageView gDepthView, VkSampler depthSampler) {
    destroyResources();
    createResources(extent, gDepthView, depthSampler);
}

void HiZSystem::createResources(VkExtent2D extent, VkImageView gDepthView, VkSampler depthSampler) {
    
    mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(extent.width, extent.height)))) + 1;

    
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_SFLOAT; 
    imageInfo.extent = {extent.width, extent.height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;

    lveDevice.createImageWithInfo(imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, hiZImage, hiZMemory);

    
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST; 
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    
    
    
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    
    
    samplerInfo.maxLod = static_cast<float>(mipLevels);
    vkCreateSampler(lveDevice.device(), &samplerInfo, nullptr, &hiZSampler);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = hiZImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R32_SFLOAT;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    vkCreateImageView(lveDevice.device(), &viewInfo, nullptr, &fullView);

    mipViews.resize(mipLevels);
    for (uint32_t i = 0; i < mipLevels; i++) {
        viewInfo.subresourceRange.baseMipLevel = i;
        viewInfo.subresourceRange.levelCount = 1;
        vkCreateImageView(lveDevice.device(), &viewInfo, nullptr, &mipViews[i]);
    }

    
    auto cmd = lveDevice.beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.image = hiZImage;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, 1};
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    lveDevice.endSingleTimeCommands(cmd);

    
    mipSets.resize(mipLevels);
    for (uint32_t i = 0; i < mipLevels; i++) {
        VkDescriptorImageInfo inputInfo{};
        inputInfo.sampler = depthSampler;
        
        inputInfo.imageView = (i == 0) ? gDepthView : mipViews[i - 1]; 
        inputInfo.imageLayout = (i == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;

        VkDescriptorImageInfo outputInfo{};
        outputInfo.imageView = mipViews[i];
        outputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

        BurnhopeDescriptorWriter(*setLayout, globalPool)
            .writeImage(0, &inputInfo)
            .writeImage(1, &outputInfo)
            .build(mipSets[i]);
    }
}

VkDescriptorImageInfo HiZSystem::getHiZImageInfo() const {
    return {hiZSampler, fullView, VK_IMAGE_LAYOUT_GENERAL};
}

void HiZSystem::compute(VkCommandBuffer cmd, VkExtent2D extent) {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

    uint32_t mipWidth = extent.width;
    uint32_t mipHeight = extent.height;

    for (uint32_t i = 0; i < mipLevels; i++) {
        
        mipWidth = std::max(1u, mipWidth / 2);
        mipHeight = std::max(1u, mipHeight / 2);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &mipSets[i], 0, nullptr);
        
        HiZPush push{};
        push.invSize = {1.0f / float(std::max(1u, mipWidth * 2)), 1.0f / float(std::max(1u, mipHeight * 2))};
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(HiZPush), &push);
 
        vkCmdDispatch(cmd, (mipWidth + 15) / 16, (mipHeight + 15) / 16, 1);

        
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier.image = hiZImage;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, i, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
}

} 
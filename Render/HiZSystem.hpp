#pragma once
#include "../Utils/Device.hpp"
#include "../Utils/Descriptors.hpp"
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "ComputeShader.hpp"
namespace burnhope
{
    class HiZSystem
    {
    public:
        HiZSystem(BurnhopeDevice &device, VkExtent2D extent, BurnhopeDescriptorPool &pool,
                  VkImageView gDepthView, VkSampler depthSampler);
        ~HiZSystem();
        void rebuild(VkExtent2D extent, VkImageView gDepthView, VkSampler depthSampler);
        void compute(VkCommandBuffer cmd, VkExtent2D extent);
        VkDescriptorImageInfo getHiZImageInfo() const;

    private:
        void createResources(VkExtent2D extent, VkImageView gDepthView, VkSampler depthSampler);
        void destroyResources();
        BurnhopeDevice &lveDevice;
        BurnhopeDescriptorPool &globalPool;
        VkImage hiZImage = VK_NULL_HANDLE;
        VkDeviceMemory hiZMemory = VK_NULL_HANDLE;
        VkImageView fullView = VK_NULL_HANDLE;
        VkSampler hiZSampler = VK_NULL_HANDLE;
        std::vector<VkImageView> mipViews;
        std::vector<VkDescriptorSet> mipSets;
        uint32_t mipLevels = 1;
        std::unique_ptr<BurnhopeDescriptorSetLayout> setLayout;
        std::unique_ptr<ComputeShader> shader;
    };
}
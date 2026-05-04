#pragma once
#include "../Utils/Device.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <fstream>
#include <algorithm>
namespace burnhope
{
    struct BHTexHeader
    {
        uint32_t width;
        uint32_t height;
        uint32_t mipCount;
        uint32_t format;
        uint32_t wrapS;
        uint32_t wrapT;
        uint32_t minFilter;
        uint32_t magFilter;
        float maxAnisotropy;
        bool isSRGB;
    };
    class BurnhopeTexture
    {
    public:
        BurnhopeTexture(BurnhopeDevice &device, const std::string &textureFilepath, bool isSRGB = true);
        BurnhopeTexture(
            BurnhopeDevice &device,
            VkFormat format,
            VkExtent3D extent,
            VkImageUsageFlags usage,
            VkSampleCountFlagBits sampleCount);
        ~BurnhopeTexture();
        BurnhopeTexture(
            BurnhopeDevice &device,
            VkFormat format,
            VkExtent3D extent,
            VkImageUsageFlags usage,
            VkSampleCountFlagBits sampleCount,
            uint32_t arrayLayers);
        BurnhopeTexture(const BurnhopeTexture &) = delete;
        BurnhopeTexture &operator=(const BurnhopeTexture &) = delete;
        VkSampler getSampler() const { return mTextureSampler; }
        VkImageView imageView() const { return mTextureImageView; }
        VkSampler sampler() const { return mTextureSampler; }
        VkImage getImage() const { return mTextureImage; }
        VkImageView getImageView() const { return mTextureImageView; }
        VkDescriptorImageInfo getImageInfo() const { return mDescriptor; }
        VkImageLayout getImageLayout() const { return mTextureLayout; }
        VkExtent3D getExtent() const { return mExtent; }
        VkFormat getFormat() const { return mFormat; }
        uint32_t arrayLayers = 1;
        void updateDescriptor();
        void transitionLayout(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout);
        void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
        static std::unique_ptr<BurnhopeTexture> createTextureFromFile(
            BurnhopeDevice &device, const std::string &filepath);
        static std::unique_ptr<BurnhopeTexture> createDataTextureFromFile(BurnhopeDevice &device, const std::string &filepath);

    private:
        void createTextureImage(const std::string &filepath, bool isSRGB = true);
        void createTextureImageView(VkImageViewType viewType);
        void createTextureSampler();
        bool loadFromBHTex(const std::string &filepath);
        void generateAndCacheBHTex(const std::string &srcPath, const std::string &cachePath, bool isSRGB);
        VkDescriptorImageInfo mDescriptor{};
        BurnhopeDevice &mDevice;
        VkImage mTextureImage = nullptr;
        VkDeviceMemory mTextureImageMemory = nullptr;
        VkImageView mTextureImageView = nullptr;
        VkSampler mTextureSampler = nullptr;
        VkFormat mFormat;
        VkImageLayout mTextureLayout;
        uint32_t mMipLevels{1};
        uint32_t mLayerCount{1};
        VkExtent3D mExtent{};
    };
}

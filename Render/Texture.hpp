#pragma once
#include "../Utils/Device.hpp"
#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <fstream>
#include <algorithm>
#include <unordered_map>
namespace burnhope
{
    struct BHTexHeader
    {
        char magic[4]; // "BHTX"
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
        bool hasAlpha;
        int packType; // 0=Raw, 1=AlbedoAlpha/Normal(BC7), 2=ORMX(BC7), 3=Normal(BC5), 4=Emissive(BC6H)
        
        // Храним пути к исходникам для пересоздания из UI
        char srcPath1[256];
        char srcPath2[256];
        char srcPath3[256];
        char srcPath4[256];
    };
    class BurnhopeTexture
    {
    public:
        static float GlobalAnisotropy;
        BurnhopeTexture(BurnhopeDevice &device, const std::string &textureFilepath, bool isSRGB = true);
        BurnhopeTexture(
            BurnhopeDevice &device,
            VkFormat format,
            VkExtent3D extent,
            VkImageUsageFlags usage,
            VkSampleCountFlagBits sampleCount);
        BurnhopeTexture(
            BurnhopeDevice &device,
            VmaAllocation aliasedMemory,
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
        VmaAllocation getMemory() const { return mTextureImageMemory; }
        VkExtent3D getExtent() const { return mExtent; }
        VkFormat getFormat() const { return mFormat; }
        uint32_t arrayLayers = 1;
        void updateDescriptor();
        void transitionLayout(VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout);
        void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels);
        static std::shared_ptr<BurnhopeTexture> createTextureFromFile(BurnhopeDevice &device, const std::string &filepath);
        static std::shared_ptr<BurnhopeTexture> createDataTextureFromFile(BurnhopeDevice &device, const std::string &filepath);

        // Утилиты для паковки текстур
        static void packORMX(const std::string& ao, const std::string& rough, const std::string& metal, const std::string& height, const std::string& outPath);
        static void packAlbedoAlpha(const std::string& albedo, const std::string& alpha, const std::string& outPath);
        static void packNormal(const std::string& normal, const std::string& outPath);
        static void packEmissive(const std::string& emissive, const std::string& outPath);
        static void rebuildFromHeader(const std::string& bhtexPath);

    private:
        void createTextureImage(const std::string &filepath, bool isSRGB = true);
        void createTextureImageView(VkImageViewType viewType);
        void createTextureSampler();
        bool loadFromBHTex(const std::string &filepath);
        void generateAndCacheBHTex(const std::string &srcPath, const std::string &cachePath, bool isSRGB);
        
        static std::unordered_map<std::string, std::weak_ptr<BurnhopeTexture>> textureCache;

        VkDescriptorImageInfo mDescriptor{};
        BurnhopeDevice &mDevice;
        VkImage mTextureImage = nullptr;
        VmaAllocation mTextureImageMemory = nullptr;
        VkImageView mTextureImageView = nullptr;
        VkSampler mTextureSampler = nullptr;
        VkFormat mFormat;
        VkImageLayout mTextureLayout;
        uint32_t mMipLevels{1};
        uint32_t mLayerCount{1};
        VkExtent3D mExtent{};
    };
}

#pragma once

#include "lve_device.hpp"

// libs
#include <vulkan/vulkan.h>

// std
#include <memory>
#include <string>

namespace burnhope {
class BurnhopeTexture {
 public:
  BurnhopeTexture(BurnhopeDevice &device, const std::string &textureFilepath);
  BurnhopeTexture(
      BurnhopeDevice &device,
      VkFormat format,
      VkExtent3D extent,
      VkImageUsageFlags usage,
      VkSampleCountFlagBits sampleCount);
  ~BurnhopeTexture();

  // delete copy constructors
  BurnhopeTexture(const BurnhopeTexture &) = delete;
  BurnhopeTexture &operator=(const BurnhopeTexture &) = delete;

  VkImageView imageView() const { return mTextureImageView; }
  VkSampler sampler() const { return mTextureSampler; }
  VkImage getImage() const { return mTextureImage; }
  VkImageView getImageView() const { return mTextureImageView; }
  VkDescriptorImageInfo getImageInfo() const { return mDescriptor; }
  VkImageLayout getImageLayout() const { return mTextureLayout; }
  VkExtent3D getExtent() const { return mExtent; }
  VkFormat getFormat() const { return mFormat; }

  void updateDescriptor();
  void transitionLayout(
      VkCommandBuffer commandBuffer, VkImageLayout oldLayout, VkImageLayout newLayout);

  static std::unique_ptr<BurnhopeTexture> createTextureFromFile(
      BurnhopeDevice &device, const std::string &filepath);

 private:
  void createTextureImage(const std::string &filepath);
  void createTextureImageView(VkImageViewType viewType);
  void createTextureSampler();

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

}  // namespace burnhope

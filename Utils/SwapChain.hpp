#pragma once

#include "Core/Types.hpp"
#include "Device.hpp"
#include <cstdint>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

class BurnhopeSwapChain final : public NonCopyable {
public:
  static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

  BurnhopeSwapChain(BurnhopeDevice& device, VkExtent2D windowExtent);
  BurnhopeSwapChain(
      BurnhopeDevice& device,
      VkExtent2D windowExtent,
      std::shared_ptr<BurnhopeSwapChain> previous);
  ~BurnhopeSwapChain();

  [[nodiscard]] VkImage image(int index) const noexcept { return images_[index]; }
  [[nodiscard]] VkImage getImage(int index) const noexcept { return image(index); }
  [[nodiscard]] VkImageView imageView(int index) const noexcept { return imageViews_[index]; }
  [[nodiscard]] VkImageView getImageView(int index) const noexcept { return imageView(index); }
  [[nodiscard]] VkImageView depthImageView(int index) const noexcept { return depthViews_[index]; }
  [[nodiscard]] VkImageView getDepthImageView(int index) const noexcept { return depthImageView(index); }

  [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }
  [[nodiscard]] VkFormat imageFormat() const noexcept { return imageFormat_; }
  [[nodiscard]] VkFormat getSwapChainImageFormat() const noexcept { return imageFormat(); }
  [[nodiscard]] VkExtent2D extent() const noexcept { return extent_; }
  [[nodiscard]] VkExtent2D getSwapChainExtent() const noexcept { return extent(); }
  [[nodiscard]] uint32_t width() const noexcept { return extent_.width; }
  [[nodiscard]] uint32_t height() const noexcept { return extent_.height; }
  [[nodiscard]] float aspectRatio() const noexcept {
    return static_cast<float>(extent_.width) / static_cast<float>(extent_.height);
  }
  [[nodiscard]] float extentAspectRatio() const noexcept { return aspectRatio(); }

  [[nodiscard]] VkResult acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex);
  [[nodiscard]] VkResult submitAndPresent(
      const VkCommandBuffer* buffers,
      uint32_t frameIndex,
      uint32_t imageIndex,
      VkFence fence = VK_NULL_HANDLE);

  [[nodiscard]] bool compareSwapFormats(const BurnhopeSwapChain& other) const noexcept {
    return depthFormat_ == other.depthFormat_ && imageFormat_ == other.imageFormat_;
  }

private:
  void init();
  void createSwapChain();
  void createImageViews();
  void createDepthResources();
  void createSyncObjects();

  [[nodiscard]] VkSurfaceFormatKHR chooseSurfaceFormat(
      std::span<const VkSurfaceFormatKHR> formats) const;
  [[nodiscard]] VkPresentModeKHR choosePresentMode(
      std::span<const VkPresentModeKHR> modes) const;
  [[nodiscard]] VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const;
  [[nodiscard]] VkFormat findDepthFormat() const;

  BurnhopeDevice& device_;
  VkExtent2D windowExtent_{};
  VkFormat imageFormat_{};
  VkFormat depthFormat_{};
  VkExtent2D extent_{};
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  std::shared_ptr<BurnhopeSwapChain> oldSwapchain_;

  std::vector<VkImage> images_;
  std::vector<VkImageView> imageViews_;
  std::vector<VkImage> depthImages_;
  std::vector<VmaAllocation> depthAllocations_;
  std::vector<VkImageView> depthViews_;

  /** Per in-flight frame slot (acquire + submit wait). */
  std::vector<VkSemaphore> imageAvailable_;
  /** Per swapchain image (submit signal + present wait). */
  std::vector<VkSemaphore> renderFinished_;
};

} // namespace burnhope

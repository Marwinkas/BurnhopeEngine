#include "SwapChain.hpp"
#include "Core/Types.hpp"
#include <algorithm>
#include <limits>

namespace burnhope {

BurnhopeSwapChain::BurnhopeSwapChain(BurnhopeDevice& device, VkExtent2D extent)
    : device_{device}, windowExtent_{extent} {
  init();
}

BurnhopeSwapChain::BurnhopeSwapChain(
    BurnhopeDevice& device,
    VkExtent2D extent,
    std::shared_ptr<BurnhopeSwapChain> previous)
    : device_{device}, windowExtent_{extent}, oldSwapchain_{std::move(previous)} {
  init();
  oldSwapchain_.reset();
}

void BurnhopeSwapChain::init() {
  createSwapChain();
  createImageViews();
  createDepthResources();
  createSyncObjects();
}

BurnhopeSwapChain::~BurnhopeSwapChain() {
  for (VkSemaphore sem : imageAvailable_) {
    vkDestroySemaphore(device_.device(), sem, nullptr);
  }
  for (VkSemaphore sem : renderFinished_) {
    vkDestroySemaphore(device_.device(), sem, nullptr);
  }
  for (VkImageView view : imageViews_) {
    vkDestroyImageView(device_.device(), view, nullptr);
  }
  for (VkImageView view : depthViews_) {
    vkDestroyImageView(device_.device(), view, nullptr);
  }
  for (std::size_t i = 0; i < depthImages_.size(); ++i) {
    vmaDestroyImage(device_.allocator(), depthImages_[i], depthAllocations_[i]);
  }
  if (swapchain_) {
    vkDestroySwapchainKHR(device_.device(), swapchain_, nullptr);
  }
}

VkResult BurnhopeSwapChain::acquireNextImage(uint32_t frameIndex, uint32_t* imageIndex) {
  return vkAcquireNextImageKHR(
      device_.device(),
      swapchain_,
      UINT64_MAX,
      imageAvailable_[frameIndex],
      VK_NULL_HANDLE,
      imageIndex);
}

VkResult BurnhopeSwapChain::submitAndPresent(
    const VkCommandBuffer* buffers,
    uint32_t frameIndex,
    uint32_t imageIndex,
    VkFence fence) {
  VkSemaphore renderDone = renderFinished_[imageIndex];

  VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &imageAvailable_[frameIndex];
  submit.pWaitDstStageMask = &waitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = buffers;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &renderDone;

  VkResult result = vkQueueSubmit(device_.graphicsQueue(), 1, &submit, fence);
  if (result != VK_SUCCESS) {
    return result;
  }

  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &renderDone;
  present.swapchainCount = 1;
  present.pSwapchains = &swapchain_;
  present.pImageIndices = &imageIndex;

  return vkQueuePresentKHR(device_.presentQueue(), &present);
}

void BurnhopeSwapChain::createSwapChain() {
  const SwapChainSupportDetails support = device_.swapChainSupport();
  const VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);
  const VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);
  extent_ = chooseExtent(support.capabilities);

  uint32_t imageCount = support.capabilities.minImageCount + 1;
  if (support.capabilities.maxImageCount > 0 &&
      imageCount > support.capabilities.maxImageCount) {
    imageCount = support.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  info.surface = device_.surface();
  info.minImageCount = imageCount;
  info.imageFormat = surfaceFormat.format;
  info.imageColorSpace = surfaceFormat.colorSpace;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.preTransform = support.capabilities.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = presentMode;
  info.clipped = VK_TRUE;
  info.oldSwapchain = oldSwapchain_ ? oldSwapchain_->swapchain_ : VK_NULL_HANDLE;

  const QueueFamilyIndices indices = device_.findPhysicalQueueFamilies();
  const uint32_t queueFamilies[] = {indices.graphicsFamily, indices.presentFamily};
  if (indices.graphicsFamily != indices.presentFamily) {
    info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 2;
    info.pQueueFamilyIndices = queueFamilies;
  }

  if (vkCreateSwapchainKHR(device_.device(), &info, nullptr, &swapchain_) != VK_SUCCESS) {
    throwVkError("vkCreateSwapchainKHR");
  }

  vkGetSwapchainImagesKHR(device_.device(), swapchain_, &imageCount, nullptr);
  images_.resize(imageCount);
  vkGetSwapchainImagesKHR(device_.device(), swapchain_, &imageCount, images_.data());
  imageFormat_ = surfaceFormat.format;
}

void BurnhopeSwapChain::createImageViews() {
  imageViews_.resize(images_.size());
  for (std::size_t i = 0; i < images_.size(); ++i) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = images_[i];
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = imageFormat_;
    info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(device_.device(), &info, nullptr, &imageViews_[i]) != VK_SUCCESS) {
      throwVkError("swapchain image view");
    }
  }
}

void BurnhopeSwapChain::createDepthResources() {
  depthFormat_ = findDepthFormat();
  depthImages_.resize(imageCount());
  depthAllocations_.resize(imageCount());
  depthViews_.resize(imageCount());

  for (std::size_t i = 0; i < depthImages_.size(); ++i) {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.extent = {extent_.width, extent_.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.format = depthFormat_;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image.samples = VK_SAMPLE_COUNT_1_BIT;

    device_.createImageWithInfo(image, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImages_[i],
                                depthAllocations_[i]);

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = depthImages_[i];
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depthFormat_;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (depthFormat_ == VK_FORMAT_D32_SFLOAT_S8_UINT || depthFormat_ == VK_FORMAT_D24_UNORM_S8_UINT) {
      view.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_.device(), &view, nullptr, &depthViews_[i]) != VK_SUCCESS) {
      throwVkError("depth image view");
    }
  }
}

void BurnhopeSwapChain::createSyncObjects() {
  imageAvailable_.resize(MAX_FRAMES_IN_FLIGHT);
  renderFinished_.resize(imageCount());
  VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  for (std::size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
    if (vkCreateSemaphore(device_.device(), &sem, nullptr, &imageAvailable_[i]) != VK_SUCCESS) {
      throwVkError("imageAvailable semaphore");
    }
  }
  for (std::size_t i = 0; i < imageCount(); ++i) {
    if (vkCreateSemaphore(device_.device(), &sem, nullptr, &renderFinished_[i]) != VK_SUCCESS) {
      throwVkError("renderFinished semaphore");
    }
  }
}

VkSurfaceFormatKHR BurnhopeSwapChain::chooseSurfaceFormat(
    std::span<const VkSurfaceFormatKHR> formats) const {
  for (const auto& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return f;
    }
  }
  return formats.front();
}

VkPresentModeKHR BurnhopeSwapChain::choosePresentMode(
    std::span<const VkPresentModeKHR> modes) const {
  for (VkPresentModeKHR mode : modes) {
    if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return mode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D BurnhopeSwapChain::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
  if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
    return caps.currentExtent;
  }
  VkExtent2D actual = windowExtent_;
  actual.width = std::clamp(actual.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  actual.height = std::clamp(actual.height, caps.minImageExtent.height, caps.maxImageExtent.height);
  return actual;
}

VkFormat BurnhopeSwapChain::findDepthFormat() const {
  constexpr VkFormat candidates[] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
                                     VK_FORMAT_D24_UNORM_S8_UINT};
  return device_.findSupportedFormat(
      candidates,
      VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

} // namespace burnhope

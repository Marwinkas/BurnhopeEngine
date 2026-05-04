#pragma once

#include "Device.hpp"
#include "SwapChain.hpp"
#include "Window.hpp"


#include <cassert>
#include <memory>
#include <vector>

namespace burnhope {
class BurnhopeRenderer {
 public:
  BurnhopeRenderer(BurnhopeWindow &window, BurnhopeDevice &device);
  ~BurnhopeRenderer();

  BurnhopeRenderer(const BurnhopeRenderer &) = delete;
  BurnhopeRenderer &operator=(const BurnhopeRenderer &) = delete;

  VkRenderPass getSwapChainRenderPass() const { return lveSwapChain->getRenderPass(); }
  float getAspectRatio() const { return lveSwapChain->extentAspectRatio(); }
  bool isFrameInProgress() const { return isFrameStarted; }

  VkCommandBuffer getCurrentCommandBuffer() const {
    assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
    return commandBuffers[currentFrameIndex];
  }

  int getFrameIndex() const {
    assert(isFrameStarted && "Cannot get frame index when frame not in progress");
    return currentFrameIndex;
  }

  VkCommandBuffer beginFrame();
  void endFrame();
  void beginSwapChainRenderPass(VkCommandBuffer commandBuffer);
  void endSwapChainRenderPass(VkCommandBuffer commandBuffer);
  VkImage getCurrentSwapChainImage() const {
      return lveSwapChain->getImage(currentImageIndex);
  }
  bool wasSwapChainRecreated() const { return swapChainRecreated; }
  VkExtent2D getSwapChainExtent() const { return lveSwapChain->getSwapChainExtent(); }
  void recreateSwapChain();
 private:
  void createCommandBuffers();
  void freeCommandBuffers();

  bool swapChainRecreated = false;
  BurnhopeWindow &lveWindow;
  BurnhopeDevice &lveDevice;
  std::unique_ptr<BurnhopeSwapChain> lveSwapChain;
  std::vector<VkCommandBuffer> commandBuffers;

  uint32_t currentImageIndex;
  int currentFrameIndex{0};
  bool isFrameStarted{false};
};
}  

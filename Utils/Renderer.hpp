#pragma once
#include "Device.hpp"
#include "SwapChain.hpp"
#include "Window.hpp"
#include <cassert>
#include <memory>
#include <vector>
namespace burnhope
{
  class BurnhopeRenderer
  {
  public:
    BurnhopeRenderer(BurnhopeWindow &window, BurnhopeDevice &device);
    ~BurnhopeRenderer();
    BurnhopeRenderer(const BurnhopeRenderer &) = delete;
    BurnhopeRenderer &operator=(const BurnhopeRenderer &) = delete;
    VkFormat getSwapChainImageFormat() const { return lveSwapChain->getSwapChainImageFormat(); }
    float getAspectRatio() const { return lveSwapChain->extentAspectRatio(); }
    bool isFrameInProgress() const { return isFrameStarted; }
    VkCommandBuffer getCurrentCommandBuffer() const
    {
      assert(isFrameStarted && "Cannot get command buffer when frame not in progress");
      return commandBuffers[currentFrameIndex];
    }
    int getFrameIndex() const
    {
      assert(isFrameStarted && "Cannot get frame index when frame not in progress");
      return currentFrameIndex;
    }
    VkCommandBuffer beginFrame();
    void endFrame();
    void beginSwapChainRendering(VkCommandBuffer commandBuffer);
    void endSwapChainRendering(VkCommandBuffer commandBuffer);
    VkImage getCurrentSwapChainImage() const
    {
      return lveSwapChain->getImage(currentImageIndex);
    }
    bool wasSwapChainRecreated() const { return swapChainRecreated; }
    [[nodiscard]] bool hadSuccessfulPresent() const noexcept { return hadSuccessfulPresent_; }
    [[nodiscard]] uint32_t consecutiveGpuFailures() const noexcept { return consecutiveGpuFailures_; }
    [[nodiscard]] bool isGpuDeviceLost() const noexcept { return gpuDeviceLost_; }
    VkExtent2D getSwapChainExtent() const { return lveSwapChain->getSwapChainExtent(); }
    void recreateSwapChain();
  private:
    void recoverFromGpuError();
    void createCommandBuffers();
    void freeCommandBuffers();
    void createInFlightFences();
    void destroyInFlightFences();
    bool swapChainRecreated = false;
    BurnhopeWindow &lveWindow;
    BurnhopeDevice &lveDevice;
    std::unique_ptr<BurnhopeSwapChain> lveSwapChain;
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkFence> inFlightFences_;
    uint32_t currentImageIndex;
    int currentFrameIndex{0};
    bool isFrameStarted{false};
    bool hadSuccessfulPresent_{false};
    uint32_t consecutiveGpuFailures_{0};
    bool gpuDeviceLost_{false};
  };
}

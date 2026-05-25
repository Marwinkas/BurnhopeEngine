#include "Renderer.hpp"
#include <array>
#include <cassert>
#include <iostream>
#include <stdexcept>
namespace burnhope
{
  BurnhopeRenderer::BurnhopeRenderer(BurnhopeWindow &window, BurnhopeDevice &device)
      : lveWindow{window}, lveDevice{device}
  {
    recreateSwapChain(); // also creates inFlightFences_
    createCommandBuffers();
  }
  BurnhopeRenderer::~BurnhopeRenderer() {
    destroyInFlightFences();
    freeCommandBuffers();
  }
  void BurnhopeRenderer::recreateSwapChain()
  {
    auto extent = lveWindow.getExtent();
    while (extent.width == 0 || extent.height == 0)
    {
      extent = lveWindow.getExtent();
      lveWindow.pollEvents();
    }
    vkDeviceWaitIdle(lveDevice.device());
    destroyInFlightFences();
    if (lveSwapChain == nullptr)
    {
      lveSwapChain = std::make_unique<BurnhopeSwapChain>(lveDevice, extent);
    }
    else
    {
      std::shared_ptr<BurnhopeSwapChain> oldSwapChain = std::move(lveSwapChain);
      lveSwapChain = std::make_unique<BurnhopeSwapChain>(lveDevice, extent, oldSwapChain);
      if (!oldSwapChain->compareSwapFormats(*lveSwapChain.get()))
      {
        throw std::runtime_error("Swap chain image(or depth) format has changed!");
      }
    }
    createInFlightFences();
    currentFrameIndex = 0;
  }
  void BurnhopeRenderer::createInFlightFences() {
    destroyInFlightFences();
    inFlightFences_.resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (VkFence& fence : inFlightFences_) {
      fence = VK_NULL_HANDLE;
      if (vkCreateFence(lveDevice.device(), &info, nullptr, &fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to create in-flight fence!");
      }
    }
  }
  void BurnhopeRenderer::destroyInFlightFences() {
    if (gpuDeviceLost_) {
      inFlightFences_.clear();
      return;
    }
    for (VkFence fence : inFlightFences_) {
      if (fence != VK_NULL_HANDLE) {
        vkDestroyFence(lveDevice.device(), fence, nullptr);
      }
    }
    inFlightFences_.clear();
  }
  void BurnhopeRenderer::createCommandBuffers()
  {
    commandBuffers.resize(BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = lveDevice.getCommandPool();
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());
    if (vkAllocateCommandBuffers(lveDevice.device(), &allocInfo, commandBuffers.data()) !=
        VK_SUCCESS)
    {
      throw std::runtime_error("failed to allocate command buffers!");
    }
  }
  void BurnhopeRenderer::freeCommandBuffers()
  {
    if (commandBuffers.empty()) {
      return;
    }
    vkFreeCommandBuffers(
        lveDevice.device(),
        lveDevice.getCommandPool(),
        static_cast<uint32_t>(commandBuffers.size()),
        commandBuffers.data());
    commandBuffers.clear();
  }
  void BurnhopeRenderer::recoverFromGpuError()
  {
    std::cerr << "[GPU] recoverFromGpuError (failures=" << consecutiveGpuFailures_ << ")\n";
    isFrameStarted = false;
    freeCommandBuffers();
    destroyInFlightFences();

    const VkResult idle = vkDeviceWaitIdle(lveDevice.device());
    if (idle == VK_ERROR_DEVICE_LOST) {
      gpuDeviceLost_ = true;
      std::cerr << "[GPU] VkDevice lost — fences/CBs cleared; stop submitting work.\n";
      return;
    }

    gpuDeviceLost_ = false;
    createCommandBuffers();
    recreateSwapChain();
  }
  VkCommandBuffer BurnhopeRenderer::beginFrame()
  {
    swapChainRecreated = false;
    assert(!isFrameStarted && "Can't call beginFrame while already in progress");

    if (gpuDeviceLost_ || inFlightFences_.empty() || commandBuffers.empty()) {
      ++consecutiveGpuFailures_;
      return nullptr;
    }
    if (currentFrameIndex < 0 ||
        static_cast<std::size_t>(currentFrameIndex) >= inFlightFences_.size()) {
      currentFrameIndex = 0;
    }

    VkFence fence = inFlightFences_[static_cast<std::size_t>(currentFrameIndex)];
    const VkResult fenceWait = vkWaitForFences(lveDevice.device(), 1, &fence, VK_TRUE, UINT64_MAX);
    if (fenceWait == VK_ERROR_DEVICE_LOST) {
      ++consecutiveGpuFailures_;
      recoverFromGpuError();
      return nullptr;
    }
    if (fenceWait != VK_SUCCESS) {
      return nullptr;
    }

    auto result = lveSwapChain->acquireNextImage(
        static_cast<uint32_t>(currentFrameIndex), &currentImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
      recreateSwapChain();
      return nullptr;
    }
    if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR)
    {
      ++consecutiveGpuFailures_;
      recoverFromGpuError();
      return nullptr;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
      return nullptr;
    }

    vkResetFences(lveDevice.device(), 1, &fence);
    isFrameStarted = true;
    auto commandBuffer = getCurrentCommandBuffer();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
      return nullptr;
    }
    return commandBuffer;
  }
  void BurnhopeRenderer::endFrame()
  {
    assert(isFrameStarted && "Can't call endFrame while frame is not in progress");
    const int frameIndex = currentFrameIndex;
    if (gpuDeviceLost_ || inFlightFences_.empty() ||
        static_cast<std::size_t>(frameIndex) >= inFlightFences_.size()) {
      isFrameStarted = false;
      ++consecutiveGpuFailures_;
      return;
    }
    VkFence fence = inFlightFences_[static_cast<std::size_t>(frameIndex)];
    auto commandBuffer = getCurrentCommandBuffer();
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
      return;
    }
    auto result = lveSwapChain->submitAndPresent(
        &commandBuffer,
        static_cast<uint32_t>(frameIndex),
        currentImageIndex,
        fence);
    isFrameStarted = false;

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        lveWindow.wasWindowResized())
    {
      lveWindow.resetWindowResizedFlag();
      swapChainRecreated = true;
      recreateSwapChain();
      return;
    }
    if (result == VK_ERROR_DEVICE_LOST || result == VK_ERROR_SURFACE_LOST_KHR)
    {
      ++consecutiveGpuFailures_;
      recoverFromGpuError();
      return;
    }
    if (result != VK_SUCCESS)
    {
      return;
    }
    hadSuccessfulPresent_ = true;
    consecutiveGpuFailures_ = 0;
    currentFrameIndex = (frameIndex + 1) % BurnhopeSwapChain::MAX_FRAMES_IN_FLIGHT;
  }
  void BurnhopeRenderer::beginSwapChainRendering(VkCommandBuffer commandBuffer)
  {
    assert(isFrameStarted && "Can't call beginSwapChainRendering if frame is not in progress");
    assert(
        commandBuffer == getCurrentCommandBuffer() &&
        "Can't begin rendering on command buffer from a different frame");

    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = lveSwapChain->getImageView(currentImageIndex);
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue.color = {0.01f, 0.01f, 0.01f, 1.0f};

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.renderArea.extent = lveSwapChain->getSwapChainExtent();
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(commandBuffer, &renderingInfo);
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(lveSwapChain->getSwapChainExtent().width);
    viewport.height = static_cast<float>(lveSwapChain->getSwapChainExtent().height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{{0, 0}, lveSwapChain->getSwapChainExtent()};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
  }
  void BurnhopeRenderer::endSwapChainRendering(VkCommandBuffer commandBuffer)
  {
    assert(isFrameStarted && "Can't call endSwapChainRendering if frame is not in progress");
    assert(
        commandBuffer == getCurrentCommandBuffer() &&
        "Can't end rendering on command buffer from a different frame");
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = getCurrentSwapChainImage();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    barrier.dstAccessMask = 0;
    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.imageMemoryBarrierCount = 1;
    depInfo.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &depInfo);
  }
}

#pragma once

#include "../../Utils/Device.hpp"
#include "../../Utils/GpuSync.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

struct RenderPassNode {
  std::string name;
  std::vector<VkImageMemoryBarrier2> barriersBefore;
  std::function<void(VkCommandBuffer)> executeFunction;
};

/** Frame passes with split vkCmdPipelineBarrier2 before each pass body. */
class FrameGraph final {
public:
  explicit FrameGraph(BurnhopeDevice& device);
  ~FrameGraph();
  FrameGraph(const FrameGraph&) = delete;
  FrameGraph& operator=(const FrameGraph&) = delete;

  static VkImageMemoryBarrier2 imageBarrier(
      VkImage image,
      VkImageLayout oldLayout,
      VkImageLayout newLayout,
      VkPipelineStageFlags2 srcStage,
      VkAccessFlags2 srcAccess,
      VkPipelineStageFlags2 dstStage,
      VkAccessFlags2 dstAccess,
      VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      uint32_t layerCount = 1,
      uint32_t mipLevels = 1) {
    return makeImageBarrier2(
        image, oldLayout, newLayout, srcAccess, dstAccess, srcStage, dstStage, aspectMask, mipLevels,
        layerCount);
  }

  static VkImageMemoryBarrier2 createImageBarrier(
      VkImage image,
      VkImageLayout oldLayout,
      VkImageLayout newLayout,
      VkPipelineStageFlags2 srcStage,
      VkAccessFlags2 srcAccess,
      VkPipelineStageFlags2 dstStage,
      VkAccessFlags2 dstAccess,
      VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      uint32_t layerCount = 1,
      uint32_t mipLevels = 1) {
    return imageBarrier(
        image, oldLayout, newLayout, srcStage, srcAccess, dstStage, dstAccess, aspectMask, layerCount,
        mipLevels);
  }

  void addPass(
      std::string_view name,
      const std::vector<VkImageMemoryBarrier2>& barriers,
      std::function<void(VkCommandBuffer)> execute);

  void addPass(
      std::string_view name,
      std::initializer_list<VkImageMemoryBarrier2> barriers,
      std::function<void(VkCommandBuffer)> execute) {
    addPass(name, std::vector<VkImageMemoryBarrier2>(barriers), std::move(execute));
  }

  void addPass(std::string_view name, std::function<void(VkCommandBuffer)> execute);

  void execute(VkCommandBuffer commandBuffer);
  void clear() noexcept;

private:
  BurnhopeDevice& device_;
  std::vector<RenderPassNode> passes_;
};

} // namespace burnhope

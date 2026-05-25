#pragma once

#include "../../Utils/BindlessRegistry.hpp"
#include "../../Utils/Core/Types.hpp"
#include "../../Utils/Device.hpp"
#include "../../Utils/Pipeline.hpp"
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

/** Graphics draw via shader objects + descriptor heap. */
class GraphicsDispatch final : public NonCopyable {
public:
  GraphicsDispatch(
      BurnhopeDevice& device,
      std::span<const std::string> shaderPaths,
      std::span<const VkPushConstantRange> pushRanges,
      PipelineConfigInfo& config)
      : device_{device} {
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 0;
    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushRanges.size());
    layoutInfo.pPushConstantRanges = pushRanges.data();
    if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayout_) !=
        VK_SUCCESS) {
      throwVkError("GraphicsDispatch: pipeline layout");
    }
    config.pipelineLayout = pipelineLayout_;
    pipeline_ = std::make_unique<BurnhopePipeline>(device_, shaderPaths, pushRanges, config);
  }

  ~GraphicsDispatch() {
    if (pipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
    }
  }

  void bind(VkCommandBuffer cmd, VkExtent2D viewportExtent = {0, 0}, uint32_t vrsMode = 0) const {
    pipeline_->bind(cmd, viewportExtent, vrsMode);
  }

  void pushConstants(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      VkShaderStageFlags /*stage*/,
      uint32_t size,
      const void* data) const {
    bindless.pushAndBind(cmd, data, size);
  }

  [[nodiscard]] VkPipelineLayout layout() const noexcept { return pipelineLayout_; }

private:
  BurnhopeDevice& device_;
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  std::unique_ptr<BurnhopePipeline> pipeline_;
};

} // namespace burnhope

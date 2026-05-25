#pragma once

#include "../../Utils/BindlessRegistry.hpp"
#include "../../Utils/Core/Types.hpp"
#include "../../Utils/Device.hpp"
#include "../../Utils/Pipeline.hpp"
#include <span>
#include <string_view>
#include <vulkan/vulkan.h>

namespace burnhope {

/** Compute via shader objects + descriptor heap (no VkDescriptorSet / VkPipeline). */
class ComputeDispatch final : public NonCopyable {
public:
  ComputeDispatch(BurnhopeDevice& device, std::string_view spirvPath, uint32_t pushConstantSize = 0)
      : device_{device} {
    createLayout(pushConstantSize);
    pipeline_ = std::make_unique<BurnhopePipeline>(
        device_, spirvPath, pipelineLayout_, pushRange_);
  }

  ~ComputeDispatch() {
    if (pipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_.device(), pipelineLayout_, nullptr);
    }
  }

  void bind(VkCommandBuffer cmd) const { pipeline_->bindCompute(cmd); }

  void pushConstants(
      VkCommandBuffer cmd,
      const BindlessRegistry& bindless,
      const void* data,
      uint32_t size) const {
    bindless.pushAndBind(cmd, data, size);
  }

  void dispatch(VkCommandBuffer cmd, uint32_t groupX, uint32_t groupY, uint32_t groupZ = 1) const {
    vkCmdDispatch(cmd, groupX, groupY, groupZ);
  }

  [[nodiscard]] VkPipelineLayout layout() const noexcept { return pipelineLayout_; }

private:
  void createLayout(uint32_t pushConstantSize) {
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 0;
    pushRange_ = {};
    if (pushConstantSize > 0) {
      pushRange_.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      pushRange_.size = pushConstantSize;
      layoutInfo.pushConstantRangeCount = 1;
      layoutInfo.pPushConstantRanges = &pushRange_;
    }
    if (vkCreatePipelineLayout(device_.device(), &layoutInfo, nullptr, &pipelineLayout_) !=
        VK_SUCCESS) {
      throwVkError("ComputeDispatch: pipeline layout");
    }
  }

  BurnhopeDevice& device_;
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  VkPushConstantRange pushRange_{};
  std::unique_ptr<BurnhopePipeline> pipeline_;
};

} // namespace burnhope

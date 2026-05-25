#pragma once

#include "Device.hpp"
#include "ShaderLibrary.hpp"
#include <array>
#include <span>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

struct PipelineConfigInfo {
  std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};
  VkPipelineViewportStateCreateInfo viewportInfo{};
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyInfo{};
  VkPipelineRasterizationStateCreateInfo rasterizationInfo{};
  VkPipelineMultisampleStateCreateInfo multisampleInfo{};
  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments{};
  VkPipelineColorBlendStateCreateInfo colorBlendInfo{};
  VkPipelineDepthStencilStateCreateInfo depthStencilInfo{};
  std::vector<VkDynamicState> dynamicStateEnables;
  VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  std::vector<VkFormat> colorAttachmentFormats;
  VkFormat depthAttachmentFormat{VK_FORMAT_UNDEFINED};
  VkFormat stencilAttachmentFormat{VK_FORMAT_UNDEFINED};
  /** DX projection + Vulkan viewport: flip Y for mesh/vert clip space. */
  bool flipViewportY{false};
};

/** Graphics/compute dispatch via VK_EXT_shader_object (no VkPipeline). */
class BurnhopePipeline final : public NonCopyable {
public:
  BurnhopePipeline(
      BurnhopeDevice& device,
      std::span<const std::string> shaderPaths,
      std::span<const VkPushConstantRange> pushRanges,
      const PipelineConfigInfo& configInfo);

  BurnhopePipeline(
      BurnhopeDevice& device,
      std::string_view compFilepath,
      VkPipelineLayout pipelineLayout,
      VkPushConstantRange pushRange);

  ~BurnhopePipeline();

  void bind(VkCommandBuffer commandBuffer, VkExtent2D viewportExtent = {0, 0}, uint32_t vrsMode = 0);
  void bindCompute(VkCommandBuffer commandBuffer);
  void applyDynamicState(VkCommandBuffer cmd, VkExtent2D viewportExtent = {0, 0}, uint32_t vrsMode = 0) const;

  static void setViewportScissor(VkCommandBuffer cmd, VkExtent2D extent);
  /** Color write masks + MRT locations for dynamic rendering + shader objects. */
  static void applyFragmentOutputState(
      VkDevice device,
      VkCommandBuffer cmd,
      const PipelineConfigInfo& cfg);
  static void defaultPipelineConfigInfo(PipelineConfigInfo& configInfo);
  static void fixOwnedPointers(PipelineConfigInfo& configInfo);
  static void enableAlphaBlending(PipelineConfigInfo& configInfo);

  [[nodiscard]] VkPipelineLayout layout() const noexcept { return pipelineLayout_; }

private:
  void createGraphicsShaders(std::span<const std::string> shaderPaths);
  void createComputeShader(
      std::string_view compFilepath,
      VkPipelineLayout pipelineLayout,
      VkPushConstantRange pushRange);

  static VkShaderStageFlagBits stageFromPath(std::string_view path);

  BurnhopeDevice& device_;
  VkPipelineLayout pipelineLayout_{VK_NULL_HANDLE};
  PipelineConfigInfo config_{};
  std::vector<VkPushConstantRange> pushRanges_{};
  std::vector<VkShaderEXT> shaders_;
  std::vector<VkShaderStageFlagBits> stages_;
  std::array<VkShaderEXT, 7> shadersByStage_{};
  VkShaderEXT computeShader_{VK_NULL_HANDLE};
};

} // namespace burnhope

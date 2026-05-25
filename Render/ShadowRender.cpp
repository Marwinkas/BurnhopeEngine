#include "ShadowRender.hpp"
#include "Model.hpp"
#include "shadow.hpp"
#include <array>
#include <vector>

namespace burnhope {

ShadowRenderSystem::ShadowRenderSystem(BurnhopeDevice& device) : device_{device} {
  PipelineConfigInfo cfg{};
  BurnhopePipeline::defaultPipelineConfigInfo(cfg);
  cfg.depthAttachmentFormat = device.findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT}, VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  cfg.colorAttachmentFormats.clear();
  cfg.depthStencilInfo.depthTestEnable = VK_TRUE;
  cfg.depthStencilInfo.depthWriteEnable = VK_TRUE;
  cfg.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ShadowVertPC)};
  static const std::array<std::string, 2> kShaderPaths{
      "shaders/shadow.vert.spv", "shaders/shadow.frag.spv"};
  static const std::array<VkPushConstantRange, 1> kPushRanges{push};
  shader_ = std::make_unique<GraphicsDispatch>(
      device_, std::span<const std::string>{kShaderPaths},
      std::span<const VkPushConstantRange>{kPushRanges}, cfg);
}

void ShadowRenderSystem::renderShadow(
    VkCommandBuffer commandBuffer,
    const BindlessRegistry& bindless,
    const ShadowVertPC& basePush,
    const float4x4& lightSpaceMatrix,
    flecs::world& registry,
    bool renderDynamicOnly) {
  ShadowVertPC push = basePush;
  push.lightSpaceMatrix = lightSpaceMatrix;
  shader_->bind(commandBuffer, {BurnhopeCSM::SHADOW_MAP_SIZE, BurnhopeCSM::SHADOW_MAP_SIZE});
  shader_->pushConstants(commandBuffer, bindless, VK_SHADER_STAGE_VERTEX_BIT, sizeof(push), &push);

  registry.each([&](flecs::entity, TransformComponent&, MeshComponent& mesh) {
    if (!mesh.model || !mesh.isVisible || !mesh.model->gpuDataReady) {
      return;
    }
    if (renderDynamicOnly && mesh.isStatic) {
      return;
    }
    mesh.model->bind(commandBuffer);
    mesh.model->draw(commandBuffer);
  });
}

} // namespace burnhope

#include "PortalRenderSystem.hpp"
#include <array>

namespace burnhope {

namespace {

PipelineConfigInfo portalPipelineConfig(BurnhopeDevice& device, bool depthOnly) {
  PipelineConfigInfo cfg{};
  BurnhopePipeline::defaultPipelineConfigInfo(cfg);
  cfg.depthAttachmentFormat = device.findSupportedFormat(
      {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  cfg.stencilAttachmentFormat = cfg.depthAttachmentFormat;
  cfg.depthStencilInfo.depthTestEnable = VK_TRUE;
  cfg.depthStencilInfo.depthWriteEnable = VK_TRUE;
  cfg.depthStencilInfo.stencilTestEnable = VK_TRUE;
  cfg.depthStencilInfo.front.compareOp = VK_COMPARE_OP_ALWAYS;
  cfg.depthStencilInfo.front.failOp = VK_STENCIL_OP_REPLACE;
  cfg.depthStencilInfo.front.passOp = VK_STENCIL_OP_REPLACE;
  cfg.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_REPLACE;
  cfg.depthStencilInfo.front.compareMask = 0xFF;
  cfg.depthStencilInfo.front.writeMask = 0xFF;
  cfg.depthStencilInfo.back = cfg.depthStencilInfo.front;
  if (depthOnly) {
    cfg.colorAttachmentFormats.clear();
  } else {
    static std::vector<VkPipelineColorBlendAttachmentState> blend(1);
    blend[0].colorWriteMask = 0;
    cfg.colorBlendInfo.attachmentCount = 1;
    cfg.colorBlendInfo.pAttachments = blend.data();
    cfg.colorAttachmentFormats = {VK_FORMAT_R16G16B16A16_SFLOAT};
  }
  cfg.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
  cfg.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(cfg.dynamicStateEnables.size());
  cfg.dynamicStateInfo.pDynamicStates = cfg.dynamicStateEnables.data();
  return cfg;
}

std::unique_ptr<BurnhopeModel> makePortalQuad(BurnhopeDevice& device) {
  Builder builder;
  builder.positions = {{0, 0, 0, 0}, {65535, 0, 0, 0}, {65535, 65535, 0, 0}, {0, 65535, 0, 0}};
  const uint32_t qTan = PackSnorm3x10_1x2(float4{0.f, 0.f, 0.f, 1.f});
  builder.attributes = {
      {PackHalf2x16(float2{0.f, 0.f}), qTan}, {PackHalf2x16(float2{1.f, 0.f}), qTan},
      {PackHalf2x16(float2{1.f, 1.f}), qTan}, {PackHalf2x16(float2{0.f, 1.f}), qTan}};
  builder.indices = {0, 1, 2, 2, 3, 0};
  SubMesh sub{};
  sub.indexCounts[0] = 6;
  sub.firstIndices[0] = 0;
  sub.aabbMin = float3{-1.f, -1.f, 0.f};
  sub.aabbMax = float3{1.f, 1.f, 0.f};
  sub.boundingRadius = 1.f;
  builder.subMeshes.push_back(sub);
  return std::make_unique<BurnhopeModel>(device, builder);
}

} // namespace

PortalRenderSystem::PortalRenderSystem(BurnhopeDevice& device) : device_{device} {
  portalModel_ = makePortalQuad(device_);
  VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PortalVertPC)};
  static const std::array<std::string, 2> kPortalShaderPaths{
      "shaders/portal.vert.spv", "shaders/portal.frag.spv"};
  static const std::array<std::string, 1> kDepthResetShaderPaths{"shaders/depth_reset.frag.spv"};
  static const std::array<VkPushConstantRange, 1> kPushRanges{push};
  auto maskCfg = portalPipelineConfig(device_, false);
  auto depthCfg = portalPipelineConfig(device_, true);
  maskShader_ = std::make_unique<GraphicsDispatch>(
      device_, std::span<const std::string>{kPortalShaderPaths},
      std::span<const VkPushConstantRange>{kPushRanges}, maskCfg);
  depthResetShader_ = std::make_unique<GraphicsDispatch>(
      device_, std::span<const std::string>{kDepthResetShaderPaths},
      std::span<const VkPushConstantRange>{kPushRanges}, depthCfg);
}

void PortalRenderSystem::drawDepthReset(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const PortalVertPC& basePush,
    const float4x4& model,
    uint32_t ref) {
  PortalVertPC push = basePush;
  push.modelMatrix = model;
  push.portalId = ref;
  depthResetShader_->bind(cmd);
  depthResetShader_->pushConstants(cmd, bindless, VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(push), &push);
  vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, ref);
  portalModel_->bind(cmd);
  portalModel_->draw(cmd);
}

void PortalRenderSystem::drawMask(
    VkCommandBuffer cmd,
    const BindlessRegistry& bindless,
    const PortalVertPC& basePush,
    const float4x4& modelMatrix,
    uint32_t refValue) {
  PortalVertPC push = basePush;
  push.modelMatrix = modelMatrix;
  push.portalId = refValue;
  maskShader_->bind(cmd);
  maskShader_->pushConstants(cmd, bindless, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                             sizeof(push), &push);
  vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, refValue);
  portalModel_->bind(cmd);
  portalModel_->draw(cmd);
}

} // namespace burnhope

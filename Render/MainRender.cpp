#include "MainRender.hpp"
#include "RenderDebug.hpp"
#include "../Utils/BindlessPush.hpp"
#include <array>
#include <iostream>
#include <vector>

namespace burnhope {

namespace {

PipelineConfigInfo makeGBufferConfig(BurnhopeDevice& device, bool stencil, bool zPrepass) {
  PipelineConfigInfo cfg{};
  BurnhopePipeline::defaultPipelineConfigInfo(cfg);
  cfg.colorBlendAttachments.resize(5);
  for (auto& a : cfg.colorBlendAttachments) {
    a.colorWriteMask =
        zPrepass ? 0u
                 : (VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                    VK_COLOR_COMPONENT_A_BIT);
    a.blendEnable = VK_FALSE;
  }
  cfg.colorBlendInfo.attachmentCount = static_cast<uint32_t>(cfg.colorBlendAttachments.size());
  cfg.colorBlendInfo.pAttachments = cfg.colorBlendAttachments.data();
  cfg.colorAttachmentFormats = {
      VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R8_UINT};
  cfg.depthAttachmentFormat = device.findSupportedFormat(
      {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}, VK_IMAGE_TILING_OPTIMAL,
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
  cfg.stencilAttachmentFormat = cfg.depthAttachmentFormat;
  if constexpr (kMinimalRenderPath) {
    cfg.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
    cfg.depthStencilInfo.depthTestEnable = VK_TRUE;
    cfg.depthStencilInfo.depthWriteEnable = VK_TRUE;
    cfg.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
    cfg.flipViewportY = true;
  } else {
    cfg.depthStencilInfo.depthTestEnable = VK_TRUE;
    cfg.depthStencilInfo.depthWriteEnable = zPrepass ? VK_TRUE : VK_FALSE;
    cfg.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  }
  cfg.depthStencilInfo.stencilTestEnable = stencil ? VK_TRUE : VK_FALSE;
  if (stencil) {
    cfg.depthStencilInfo.front.compareOp = VK_COMPARE_OP_EQUAL;
    cfg.depthStencilInfo.front.failOp = VK_STENCIL_OP_KEEP;
    cfg.depthStencilInfo.front.passOp = VK_STENCIL_OP_KEEP;
    cfg.depthStencilInfo.front.depthFailOp = VK_STENCIL_OP_KEEP;
    cfg.depthStencilInfo.front.compareMask = 0xFF;
    cfg.depthStencilInfo.front.writeMask = 0x00;
    cfg.depthStencilInfo.back = cfg.depthStencilInfo.front;
  }
  cfg.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR);
  if (stencil) {
    cfg.dynamicStateEnables.push_back(VK_DYNAMIC_STATE_STENCIL_REFERENCE);
  }
  cfg.dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(cfg.dynamicStateEnables.size());
  cfg.dynamicStateInfo.pDynamicStates = cfg.dynamicStateEnables.data();
  return cfg;
}

std::unique_ptr<GraphicsDispatch> makeGBufferShader(
    BurnhopeDevice& device,
    bool stencil,
    bool zPrepass) {
  VkPushConstantRange push{VK_SHADER_STAGE_ALL, 0, kGfxHeapPushBytes};
  auto cfg = makeGBufferConfig(device, stencil, zPrepass);
  static const std::array<std::string, 3> kShaderPaths{
      "shaders/gbuffer.task.spv", "shaders/gbuffer.mesh.spv", "shaders/gbuffer.frag.spv"};
  static const std::array<VkPushConstantRange, 1> kPushRanges{push};
  return std::make_unique<GraphicsDispatch>(
      device, std::span<const std::string>{kShaderPaths},
      std::span<const VkPushConstantRange>{kPushRanges}, cfg);
}

} // namespace

GeometryRenderSystem::GeometryRenderSystem(BurnhopeDevice& device) : device_{device} {
  shader_ = makeGBufferShader(device_, false, false);
  zPrepassShader_ = makeGBufferShader(device_, false, true);
  portalShader_ = makeGBufferShader(device_, true, false);
  zPrepassPortalShader_ = makeGBufferShader(device_, true, true);
}

void GeometryRenderSystem::renderEntities(
    FrameInfo& frameInfo,
    const BindlessRegistry& bindless,
    const GfxHeapPC& heapPush,
    uint32_t totalSubMeshCount,
    bool useStencil,
    uint32_t vrsMode,
    bool isZPrepass) {
  GraphicsDispatch* pipe = shader_.get();
  if (isZPrepass) {
    pipe = useStencil ? zPrepassPortalShader_.get() : zPrepassShader_.get();
  } else if (useStencil) {
    pipe = portalShader_.get();
  }

  const VkExtent2D vp{static_cast<uint32_t>(frameInfo.camera.width),
                      static_cast<uint32_t>(frameInfo.camera.height)};
  alignas(16) uint8_t wirePush[kGfxHeapPushBytes]{};
  packGfxHeapPush(wirePush, heapPush);
  bindless.pushAndBind(frameInfo.commandBuffer, wirePush, kGfxHeapPushBytes);
  pipe->bind(frameInfo.commandBuffer, vp, vrsMode);

  auto pfnDraw = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
      vkGetDeviceProcAddr(device_.device(), "vkCmdDrawMeshTasksEXT"));
  if (!pfnDraw) {
    static bool logged = false;
    if (!logged) {
      std::cerr << "[G-Buffer] vkCmdDrawMeshTasksEXT unavailable — no geometry drawn.\n";
      logged = true;
    }
    return;
  }
  if (totalSubMeshCount == 0) {
    return;
  }
  pfnDraw(frameInfo.commandBuffer, totalSubMeshCount, 1, 1);
  if constexpr (kMinimalRenderPath) {
    static uint32_t lastCount = 0;
    static bool pushLogOnce = false;
    if (!pushLogOnce) {
      std::cerr << "[G-Buffer] GfxHeapPC push bytes=" << kGfxHeapPushBytes
                << " (SPIR-V push block, BDA geom+matrix)\n";
      pushLogOnce = true;
    }
    if (totalSubMeshCount != lastCount) {
      std::cerr << "[G-Buffer] DrawMeshTasksEXT groups=" << totalSubMeshCount
                << " sceneBda=0x" << std::hex << heapPush.sceneAddressesBda << std::dec << '\n';
      lastCount = totalSubMeshCount;
    }
  }
}

} // namespace burnhope

#include "Pipeline.hpp"
#include "../Render/Model.hpp"
#include "VkExtDispatch.hpp"
#include <array>
#include <span>

namespace burnhope {

namespace {

PFN_vkCmdSetVertexInputEXT pfnSetVertexInputEXT = nullptr;
PFN_vkCmdSetPolygonModeEXT pfnSetPolygonModeEXT = nullptr;
PFN_vkCmdSetRasterizationSamplesEXT pfnSetRasterizationSamplesEXT = nullptr;
PFN_vkCmdSetSampleMaskEXT pfnSetSampleMaskEXT = nullptr;
PFN_vkCmdSetAlphaToCoverageEnableEXT pfnSetAlphaToCoverageEnableEXT = nullptr;
PFN_vkCmdSetColorBlendEnableEXT pfnSetColorBlendEnableEXT = nullptr;
PFN_vkCmdSetColorWriteMaskEXT pfnSetColorWriteMaskEXT = nullptr;
PFN_vkCmdSetColorBlendEquationEXT pfnSetColorBlendEquationEXT = nullptr;
PFN_vkCmdSetFragmentShadingRateKHR pfnSetFragmentShadingRateKHR = nullptr;

void ensureGfxDynamicProcs(VkDevice device) {
  if (!pfnSetVertexInputEXT) {
    pfnSetVertexInputEXT = reinterpret_cast<PFN_vkCmdSetVertexInputEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetVertexInputEXT"));
    pfnSetPolygonModeEXT = reinterpret_cast<PFN_vkCmdSetPolygonModeEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetPolygonModeEXT"));
    pfnSetRasterizationSamplesEXT = reinterpret_cast<PFN_vkCmdSetRasterizationSamplesEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetRasterizationSamplesEXT"));
    pfnSetSampleMaskEXT = reinterpret_cast<PFN_vkCmdSetSampleMaskEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetSampleMaskEXT"));
    pfnSetAlphaToCoverageEnableEXT = reinterpret_cast<PFN_vkCmdSetAlphaToCoverageEnableEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetAlphaToCoverageEnableEXT"));
    pfnSetColorBlendEnableEXT = reinterpret_cast<PFN_vkCmdSetColorBlendEnableEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetColorBlendEnableEXT"));
    pfnSetColorWriteMaskEXT = reinterpret_cast<PFN_vkCmdSetColorWriteMaskEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetColorWriteMaskEXT"));
    pfnSetColorBlendEquationEXT = reinterpret_cast<PFN_vkCmdSetColorBlendEquationEXT>(
        vkGetDeviceProcAddr(device, "vkCmdSetColorBlendEquationEXT"));
    pfnSetFragmentShadingRateKHR = reinterpret_cast<PFN_vkCmdSetFragmentShadingRateKHR>(
        vkGetDeviceProcAddr(device, "vkCmdSetFragmentShadingRateKHR"));
  }
}

void applyColorBlendDynamicState(
    VkCommandBuffer cmd,
    const PipelineConfigInfo& cfg) {
  if (cfg.colorAttachmentFormats.empty()) {
    return;
  }
  std::span<const VkPipelineColorBlendAttachmentState> attachments;
  if (!cfg.colorBlendAttachments.empty()) {
    attachments = cfg.colorBlendAttachments;
  } else if (cfg.colorBlendInfo.pAttachments && cfg.colorBlendInfo.attachmentCount > 0) {
    attachments = {cfg.colorBlendInfo.pAttachments, cfg.colorBlendInfo.attachmentCount};
  } else {
    attachments = {&cfg.colorBlendAttachment, 1};
  }
  const uint32_t count = static_cast<uint32_t>(attachments.size());
  std::vector<VkBool32> blendEnable(count);
  std::vector<VkColorComponentFlags> writeMasks(count);
  std::vector<VkColorBlendEquationEXT> equations(count);
  for (uint32_t i = 0; i < count; ++i) {
    const auto& att = attachments[i];
    blendEnable[i] = att.blendEnable;
    writeMasks[i] = att.colorWriteMask;
    equations[i] = {};
    equations[i].colorBlendOp = att.colorBlendOp;
    equations[i].srcColorBlendFactor = att.srcColorBlendFactor;
    equations[i].dstColorBlendFactor = att.dstColorBlendFactor;
    equations[i].alphaBlendOp = att.alphaBlendOp;
    equations[i].srcAlphaBlendFactor = att.srcAlphaBlendFactor;
    equations[i].dstAlphaBlendFactor = att.dstAlphaBlendFactor;
  }
  if (pfnSetColorBlendEnableEXT) {
    pfnSetColorBlendEnableEXT(cmd, 0, count, blendEnable.data());
  }
  if (pfnSetColorWriteMaskEXT) {
    pfnSetColorWriteMaskEXT(cmd, 0, count, writeMasks.data());
  }
  if (pfnSetColorBlendEquationEXT) {
    pfnSetColorBlendEquationEXT(cmd, 0, count, equations.data());
  }
}

constexpr std::array kGraphicsStageOrder = {
    VK_SHADER_STAGE_VERTEX_BIT,
    VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
    VK_SHADER_STAGE_GEOMETRY_BIT,
    VK_SHADER_STAGE_TASK_BIT_EXT,
    VK_SHADER_STAGE_MESH_BIT_EXT,
    VK_SHADER_STAGE_FRAGMENT_BIT,
};

int stageOrderIndex(VkShaderStageFlagBits stage) {
  for (size_t i = 0; i < kGraphicsStageOrder.size(); ++i) {
    if (kGraphicsStageOrder[i] == stage) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void applyShaderObjectDynamicState(
    VkCommandBuffer cmd,
    VkDevice device,
    const PipelineConfigInfo& cfg,
    VkExtent2D viewportExtent,
    uint32_t vrsMode) {
  ensureGfxDynamicProcs(device);
  vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);
  vkCmdSetPrimitiveTopology(cmd, cfg.inputAssemblyInfo.topology);
  vkCmdSetPrimitiveRestartEnable(cmd, cfg.inputAssemblyInfo.primitiveRestartEnable);

  std::vector<VkVertexInputBindingDescription2EXT> bindings2;
  bindings2.reserve(cfg.bindingDescriptions.size());
  for (const auto& b : cfg.bindingDescriptions) {
    VkVertexInputBindingDescription2EXT desc{VK_STRUCTURE_TYPE_VERTEX_INPUT_BINDING_DESCRIPTION_2_EXT};
    desc.binding = b.binding;
    desc.stride = b.stride;
    desc.inputRate = b.inputRate;
    desc.divisor = 1;
    bindings2.push_back(desc);
  }
  std::vector<VkVertexInputAttributeDescription2EXT> attrs2;
  attrs2.reserve(cfg.attributeDescriptions.size());
  for (const auto& a : cfg.attributeDescriptions) {
    VkVertexInputAttributeDescription2EXT desc{VK_STRUCTURE_TYPE_VERTEX_INPUT_ATTRIBUTE_DESCRIPTION_2_EXT};
    desc.location = a.location;
    desc.binding = a.binding;
    desc.format = a.format;
    desc.offset = a.offset;
    attrs2.push_back(desc);
  }
  if (pfnSetVertexInputEXT) {
    pfnSetVertexInputEXT(
        cmd,
        static_cast<uint32_t>(bindings2.size()),
        bindings2.empty() ? nullptr : bindings2.data(),
        static_cast<uint32_t>(attrs2.size()),
        attrs2.empty() ? nullptr : attrs2.data());
  }

  vkCmdSetCullMode(cmd, cfg.rasterizationInfo.cullMode);
  vkCmdSetFrontFace(cmd, cfg.rasterizationInfo.frontFace);
  vkCmdSetDepthTestEnable(cmd, cfg.depthStencilInfo.depthTestEnable);
  vkCmdSetDepthWriteEnable(cmd, cfg.depthStencilInfo.depthWriteEnable);
  vkCmdSetDepthCompareOp(cmd, cfg.depthStencilInfo.depthCompareOp);
  vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
  vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
  vkCmdSetStencilTestEnable(cmd, cfg.depthStencilInfo.stencilTestEnable);
  vkCmdSetStencilOp(
      cmd,
      VK_STENCIL_FACE_FRONT_BIT,
      cfg.depthStencilInfo.front.failOp,
      cfg.depthStencilInfo.front.passOp,
      cfg.depthStencilInfo.front.depthFailOp,
      cfg.depthStencilInfo.front.compareOp);
  vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_BIT, cfg.depthStencilInfo.front.writeMask);
  vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_BIT, cfg.depthStencilInfo.front.reference);
  if (pfnSetPolygonModeEXT) {
    pfnSetPolygonModeEXT(cmd, cfg.rasterizationInfo.polygonMode);
  }
  if (pfnSetRasterizationSamplesEXT) {
    pfnSetRasterizationSamplesEXT(cmd, cfg.multisampleInfo.rasterizationSamples);
  }
  if (pfnSetSampleMaskEXT) {
    const VkSampleMask sampleMask = 0xFFFFFFFFu;
    pfnSetSampleMaskEXT(cmd, cfg.multisampleInfo.rasterizationSamples, &sampleMask);
  }
  if (pfnSetAlphaToCoverageEnableEXT) {
    pfnSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);
  }

  const uint32_t vpW = viewportExtent.width > 0 ? viewportExtent.width : 1;
  const uint32_t vpH = viewportExtent.height > 0 ? viewportExtent.height : 1;
  VkViewport viewport{};
  viewport.width = static_cast<float>(vpW);
  if (cfg.flipViewportY) {
    viewport.y = static_cast<float>(vpH);
    viewport.height = -static_cast<float>(vpH);
  } else {
    viewport.height = static_cast<float>(vpH);
  }
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewportWithCount(cmd, 1, &viewport);
  VkRect2D scissor{{0, 0}, {vpW, vpH}};
  vkCmdSetScissorWithCount(cmd, 1, &scissor);
  applyColorBlendDynamicState(cmd, cfg);

  if (pfnSetFragmentShadingRateKHR) {
    VkExtent2D fragmentSize{1, 1};
    if (vrsMode == 1) {
      fragmentSize = {2, 2};
    }
    const VkFragmentShadingRateCombinerOpKHR combiner[2] = {
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR,
        VK_FRAGMENT_SHADING_RATE_COMBINER_OP_KEEP_KHR};
    pfnSetFragmentShadingRateKHR(cmd, &fragmentSize, combiner);
  }
}

} // namespace

void BurnhopePipeline::applyFragmentOutputState(
    VkDevice /*device*/,
    VkCommandBuffer cmd,
    const PipelineConfigInfo& cfg) {
  applyColorBlendDynamicState(cmd, cfg);
}

BurnhopePipeline::BurnhopePipeline(
    BurnhopeDevice& device,
    std::span<const std::string> shaderPaths,
    std::span<const VkPushConstantRange> pushRanges,
    const PipelineConfigInfo& configInfo)
    : device_{device},
      config_{configInfo},
      pipelineLayout_{configInfo.pipelineLayout},
      pushRanges_{pushRanges.begin(), pushRanges.end()} {
  fixOwnedPointers(config_);
  createGraphicsShaders(shaderPaths);
}

BurnhopePipeline::BurnhopePipeline(
    BurnhopeDevice& device,
    std::string_view compFilepath,
    VkPipelineLayout pipelineLayout,
    VkPushConstantRange pushRange)
    : device_{device}, pipelineLayout_{pipelineLayout}, pushRanges_{pushRange} {
  createComputeShader(compFilepath, pipelineLayout, pushRange);
}

BurnhopePipeline::~BurnhopePipeline() {
  for (VkShaderEXT s : shaders_) {
    ShaderLibrary::destroyShader(device_, s);
  }
  ShaderLibrary::destroyShader(device_, computeShader_);
}

VkShaderStageFlagBits BurnhopePipeline::stageFromPath(std::string_view path) {
  if (path.find(".vert") != std::string_view::npos) {
    return VK_SHADER_STAGE_VERTEX_BIT;
  }
  if (path.find(".frag") != std::string_view::npos) {
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  if (path.find(".mesh") != std::string_view::npos) {
    return VK_SHADER_STAGE_MESH_BIT_EXT;
  }
  if (path.find(".task") != std::string_view::npos) {
    return VK_SHADER_STAGE_TASK_BIT_EXT;
  }
  return VK_SHADER_STAGE_VERTEX_BIT;
}

void BurnhopePipeline::createGraphicsShaders(std::span<const std::string> shaderPaths) {
  if (pipelineLayout_ == VK_NULL_HANDLE) {
    throwVkError("BurnhopePipeline: missing pipelineLayout");
  }

  std::vector<VkShaderStageFlagBits> orderedStages;
  orderedStages.reserve(shaderPaths.size());
  for (const std::string& path : shaderPaths) {
    if (!path.empty()) {
      orderedStages.push_back(stageFromPath(path));
    }
  }

  for (const std::string& path : shaderPaths) {
    if (path.empty()) {
      continue;
    }
    auto spirv = ShaderLibrary::loadSpirv(path);
    const auto stage = stageFromPath(path);

    VkShaderStageFlags nextStage = 0;
    for (size_t i = 0; i < orderedStages.size(); ++i) {
      if (orderedStages[i] == stage && i + 1 < orderedStages.size()) {
        nextStage = orderedStages[i + 1];
        break;
      }
    }

    VkShaderEXT shader = ShaderLibrary::createShader(
        device_, spirv, stage, "main", 0, {}, pushRanges_, nextStage);
    shaders_.push_back(shader);
    stages_.push_back(stage);
    const int idx = stageOrderIndex(stage);
    if (idx >= 0) {
      shadersByStage_[static_cast<size_t>(idx)] = shader;
    }
  }
}

void BurnhopePipeline::createComputeShader(
    std::string_view compFilepath,
    VkPipelineLayout pipelineLayout,
    VkPushConstantRange pushRange) {
  pipelineLayout_ = pipelineLayout;
  pushRanges_ = {pushRange};
  auto spirv = ShaderLibrary::loadSpirv(compFilepath);
  computeShader_ = ShaderLibrary::createShader(
      device_,
      spirv,
      VK_SHADER_STAGE_COMPUTE_BIT,
      "main",
      VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT);
}

void BurnhopePipeline::applyDynamicState(
    VkCommandBuffer cmd,
    VkExtent2D viewportExtent,
    uint32_t vrsMode) const {
  applyShaderObjectDynamicState(cmd, device_.device(), config_, viewportExtent, vrsMode);
}

void BurnhopePipeline::bind(
    VkCommandBuffer commandBuffer,
    VkExtent2D viewportExtent,
    uint32_t vrsMode) {
  if (shaders_.empty()) {
    return;
  }

  // Must bind every graphics stage (real shader or VK_NULL_HANDLE) so stale stages from a
  // prior bind (e.g. shadow VERTEX left on while drawing mesh tasks) are cleared.
  std::array<VkShaderEXT, kGraphicsStageOrder.size()> stageShaders{};
  for (size_t i = 0; i < kGraphicsStageOrder.size(); ++i) {
    stageShaders[i] = shadersByStage_[i];
  }
  ShaderLibrary::bindGraphics(
      commandBuffer,
      std::span<const VkShaderEXT>{stageShaders.data(), stageShaders.size()},
      std::span<const VkShaderStageFlagBits>{kGraphicsStageOrder.data(), kGraphicsStageOrder.size()});
  applyDynamicState(commandBuffer, viewportExtent, vrsMode);
}

void BurnhopePipeline::bindCompute(VkCommandBuffer commandBuffer) {
  ShaderLibrary::bindCompute(commandBuffer, computeShader_);
}

void BurnhopePipeline::setViewportScissor(VkCommandBuffer cmd, VkExtent2D extent) {
  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewportWithCount(cmd, 1, &viewport);
  VkRect2D scissor{{0, 0}, extent};
  vkCmdSetScissorWithCount(cmd, 1, &scissor);
}

void BurnhopePipeline::fixOwnedPointers(PipelineConfigInfo& configInfo) {
  if (!configInfo.colorBlendAttachments.empty()) {
    configInfo.colorBlendInfo.attachmentCount =
        static_cast<uint32_t>(configInfo.colorBlendAttachments.size());
    configInfo.colorBlendInfo.pAttachments = configInfo.colorBlendAttachments.data();
  } else {
    configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;
    if (configInfo.colorBlendInfo.attachmentCount == 0) {
      configInfo.colorBlendInfo.attachmentCount = 1;
    }
  }
  if (!configInfo.dynamicStateEnables.empty()) {
    configInfo.dynamicStateInfo.dynamicStateCount =
        static_cast<uint32_t>(configInfo.dynamicStateEnables.size());
    configInfo.dynamicStateInfo.pDynamicStates = configInfo.dynamicStateEnables.data();
  }
}

void BurnhopePipeline::defaultPipelineConfigInfo(PipelineConfigInfo& configInfo) {
  configInfo.inputAssemblyInfo = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  configInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  configInfo.inputAssemblyInfo.primitiveRestartEnable = VK_FALSE;

  configInfo.viewportInfo = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  configInfo.viewportInfo.viewportCount = 1;
  configInfo.viewportInfo.scissorCount = 1;

  configInfo.rasterizationInfo = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  configInfo.rasterizationInfo.polygonMode = VK_POLYGON_MODE_FILL;
  configInfo.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
  configInfo.rasterizationInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  configInfo.rasterizationInfo.lineWidth = 1.0f;

  configInfo.multisampleInfo = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  configInfo.multisampleInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  configInfo.colorBlendAttachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;
  configInfo.colorBlendInfo = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  configInfo.colorBlendInfo.attachmentCount = 1;
  configInfo.colorBlendInfo.pAttachments = &configInfo.colorBlendAttachment;

  configInfo.depthStencilInfo = {VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  configInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
  configInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
  configInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS;
  configInfo.depthStencilInfo.stencilTestEnable = VK_FALSE;
  configInfo.depthStencilInfo.front = {
      VK_STENCIL_OP_KEEP, VK_STENCIL_OP_REPLACE, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS, 0xff, 0xff,
      1};
  configInfo.depthStencilInfo.back = configInfo.depthStencilInfo.front;

  configInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  configInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
}

void BurnhopePipeline::enableAlphaBlending(PipelineConfigInfo& configInfo) {
  configInfo.colorBlendAttachment.blendEnable = VK_TRUE;
  configInfo.colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  configInfo.colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  configInfo.colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
  configInfo.colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  configInfo.colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  configInfo.colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

} // namespace burnhope

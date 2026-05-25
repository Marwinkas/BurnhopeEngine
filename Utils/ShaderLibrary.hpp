#pragma once

#include "Device.hpp"
#include "Core/Types.hpp"
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan.h>

namespace burnhope {

/** SPIR-V load + VK_EXT_shader_object (no VkPipeline). */
class ShaderLibrary final : public NonCopyable {
public:
  static std::vector<std::byte> loadSpirv(std::string_view path);

  static VkShaderEXT createShader(
      BurnhopeDevice& device,
      std::span<const std::byte> spirv,
      VkShaderStageFlagBits stage,
      std::string_view entryPoint = "main",
      VkShaderCreateFlagsEXT extraFlags = 0,
      std::span<VkDescriptorSetLayout> setLayouts = {},
      std::span<VkPushConstantRange> pushRanges = {},
      VkShaderStageFlags nextStage = 0);

  /** One vkCreateShadersEXT call with VK_SHADER_CREATE_LINK_STAGE_BIT_EXT (task→mesh→frag). */
  static std::vector<VkShaderEXT> createLinkedShaderGroup(
      BurnhopeDevice& device,
      std::span<const std::span<const std::byte>> spirvPerStage,
      std::span<const VkShaderStageFlagBits> stages,
      std::span<const VkShaderStageFlags> nextStages);

  static void destroyShader(BurnhopeDevice& device, VkShaderEXT shader);

  static void bindCompute(VkCommandBuffer cmd, VkShaderEXT shader);
  static void bindGraphics(VkCommandBuffer cmd, std::span<const VkShaderEXT> stages,
                           std::span<const VkShaderStageFlagBits> stageBits);
};

} // namespace burnhope

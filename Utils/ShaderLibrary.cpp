#include "ShaderLibrary.hpp"
#include "BindlessPush.hpp"
#include "VkExtDispatch.hpp"
#include <array>
#include <fstream>

namespace burnhope {

namespace {

void fillHeapMapping(
    BurnhopeDevice& device,
    VkShaderDescriptorSetAndBindingMappingInfoEXT& info,
    std::array<VkDescriptorSetAndBindingMappingEXT, 2>& mappings) {
  const uint32_t heapSlotStride = static_cast<uint32_t>(device.resourceHeapDescriptorStride());
  const uint32_t samplerStride = static_cast<uint32_t>(
      device.descriptorSize(VK_DESCRIPTOR_TYPE_SAMPLER));

  VkDescriptorMappingSourcePushIndexEXT heapPush{};
  heapPush.heapOffset = 0;
  // Resource heap base index @ GfxHeapPC.heapResourceBase (180), not meshProjection @0.
  heapPush.pushOffset = kGfxWireHeapResourceBase;
  // Binding 2 __slang_resource_heap mixes OpTypeImage + buffers — stride must match imageDescriptorAlignment (VUID-11251).
  heapPush.heapIndexStride = heapSlotStride;
  heapPush.heapArrayStride = heapSlotStride;
  heapPush.samplerHeapOffset = 0;
  heapPush.samplerPushOffset = kGfxWireDefaultSampler;
  heapPush.samplerHeapIndexStride = samplerStride;
  heapPush.samplerHeapArrayStride = samplerStride;

  for (VkDescriptorSetAndBindingMappingEXT& m : mappings) {
    m = {};
    m.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_AND_BINDING_MAPPING_EXT;
    m.resourceMask = VK_SPIRV_RESOURCE_TYPE_ALL_EXT;
    m.source = VK_DESCRIPTOR_MAPPING_SOURCE_HEAP_WITH_PUSH_INDEX_EXT;
    m.sourceData.pushIndex = heapPush;
    m.descriptorSet = 0;
    m.bindingCount = 1;
  }
  mappings[0].firstBinding = 0;
  mappings[1].firstBinding = 2;

  info = {};
  info.sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT;
  info.mappingCount = static_cast<uint32_t>(mappings.size());
  info.pMappings = mappings.data();
}

} // namespace

std::vector<std::byte> ShaderLibrary::loadSpirv(std::string_view path) {
  std::ifstream file{std::string{path}, std::ios::binary | std::ios::ate};
  if (!file) {
    throwVkError(std::string{"ShaderLibrary: cannot open "} + std::string{path});
  }
  const auto size = static_cast<std::size_t>(file.tellg());
  std::vector<std::byte> code(size);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(code.data()), static_cast<std::streamsize>(size));
  return code;
}

VkShaderEXT ShaderLibrary::createShader(
    BurnhopeDevice& device,
    std::span<const std::byte> spirv,
    VkShaderStageFlagBits stage,
    std::string_view entryPoint,
    VkShaderCreateFlagsEXT extraFlags,
    std::span<VkDescriptorSetLayout> setLayouts,
    std::span<VkPushConstantRange> /*pushRanges*/,
    VkShaderStageFlags nextStage) {
  std::array<VkDescriptorSetAndBindingMappingEXT, 2> heapMappings{};
  VkShaderDescriptorSetAndBindingMappingInfoEXT heapMappingInfo{};
  fillHeapMapping(device, heapMappingInfo, heapMappings);

  VkShaderCreateInfoEXT info{VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
  info.pNext = &heapMappingInfo;
  info.flags = extraFlags | VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT;
  info.stage = stage;
  info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
  info.codeSize = spirv.size();
  info.pCode = spirv.data();
  info.pName = entryPoint.data();
  info.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  info.pSetLayouts = setLayouts.data();
  // Heap push uses vkCmdPushDataEXT; VkShaderCreateInfoEXT must not declare push ranges.
  info.pushConstantRangeCount = 0;
  info.pPushConstantRanges = nullptr;
  info.nextStage = nextStage;

  VkShaderEXT shader = VK_NULL_HANDLE;
  if (vkext::get().createShadersEXT(device.device(), 1, &info, nullptr, &shader) != VK_SUCCESS) {
    throwVkError("vkCreateShadersEXT");
  }
  return shader;
}

std::vector<VkShaderEXT> ShaderLibrary::createLinkedShaderGroup(
    BurnhopeDevice& device,
    std::span<const std::span<const std::byte>> spirvPerStage,
    std::span<const VkShaderStageFlagBits> stages,
    std::span<const VkShaderStageFlags> nextStages) {
  const uint32_t count = static_cast<uint32_t>(stages.size());
  if (count == 0 || spirvPerStage.size() != count) {
    return {};
  }

  std::array<VkDescriptorSetAndBindingMappingEXT, 2> heapMappings{};
  VkShaderDescriptorSetAndBindingMappingInfoEXT heapMappingInfo{};
  fillHeapMapping(device, heapMappingInfo, heapMappings);

  std::vector<VkShaderCreateInfoEXT> infos(count);
  for (uint32_t i = 0; i < count; ++i) {
    VkShaderCreateInfoEXT& info = infos[i];
    info = {VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT};
    info.pNext = &heapMappingInfo;
    info.flags = VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT | VK_SHADER_CREATE_LINK_STAGE_BIT_EXT;
    info.stage = stages[i];
    info.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    info.codeSize = spirvPerStage[i].size();
    info.pCode = spirvPerStage[i].data();
    info.pName = "main";
    info.nextStage = i < nextStages.size() ? nextStages[i] : 0;
  }

  std::vector<VkShaderEXT> shaders(count);
  if (vkext::get().createShadersEXT(device.device(), count, infos.data(), nullptr, shaders.data()) !=
      VK_SUCCESS) {
    throwVkError("vkCreateShadersEXT (linked group)");
  }
  return shaders;
}

void ShaderLibrary::destroyShader(BurnhopeDevice& device, VkShaderEXT shader) {
  if (shader) {
    vkext::get().destroyShaderEXT(device.device(), shader, nullptr);
  }
}

void ShaderLibrary::bindCompute(VkCommandBuffer cmd, VkShaderEXT shader) {
  const VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
  vkext::get().cmdBindShadersEXT(cmd, 1, &stage, &shader);
}

void ShaderLibrary::bindGraphics(
    VkCommandBuffer cmd,
    std::span<const VkShaderEXT> stages,
    std::span<const VkShaderStageFlagBits> stageBits) {
  vkext::get().cmdBindShadersEXT(
      cmd, static_cast<uint32_t>(stageBits.size()), stageBits.data(), stages.data());
}

} // namespace burnhope

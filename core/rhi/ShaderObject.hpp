#pragma once

#include "rhi/Device.hpp"

namespace burnhope {

struct ShaderExt {
    VkShaderEXT handle = VK_NULL_HANDLE;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_FRAGMENT_BIT;
};

struct ShaderCreateDesc {
    const char* path = nullptr;
    VkShaderStageFlagBits stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkShaderStageFlags nextStage = 0;
    VkShaderCreateFlagsEXT extraFlags = 0;
    uint32_t mappingCount = 0;
    const VkDescriptorSetAndBindingMappingEXT* mappings = nullptr;
    const char* entry = nullptr;
};

[[nodiscard]] bool shaderCreate(Device& d, const ShaderCreateDesc& desc, ShaderExt& out);

[[nodiscard]] bool shaderCreateSpvFile(
    Device& d,
    const char* path,
    VkShaderStageFlagBits stage,
    VkShaderStageFlags nextStage,
    ShaderExt& out);

void shaderDestroy(Device& d, ShaderExt& s);

void cmdBindMeshFrag(VkCommandBuffer cmd, VkShaderEXT mesh, VkShaderEXT frag);
void cmdBindVertFrag(VkCommandBuffer cmd, VkShaderEXT vert, VkShaderEXT frag, bool nullMesh);
void cmdBindCompute(VkCommandBuffer cmd, VkShaderEXT cs);
void cmdSetGraphicsDynamic(
    VkCommandBuffer cmd,
    VkExtent2D extent,
    uint32_t colorAttCount,
    bool depthTest,
    bool alphaBlend = false,
    uint8_t blendMode = 0);

} // namespace burnhope

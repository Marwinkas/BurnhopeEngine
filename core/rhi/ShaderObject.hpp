#pragma once

#include "rhi/Device.hpp"

namespace burnhope {

constexpr uint32_t kMaxColorAttachments = 8;

enum class BlendMode : uint8_t { None = 0, Alpha = 1, Premultiplied = 2, Additive = 3, Multiply = 4 };

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
    uint32_t pushBytes = 0;
    VkShaderStageFlags pushStages = 0;
};

[[nodiscard]] bool shaderCreate(Device& d, const ShaderCreateDesc& desc, ShaderExt& out);
[[nodiscard]] bool shaderCreateCode(Device& d, const ShaderCreateDesc& desc, const void* code, uint64_t codeBytes, ShaderExt& out);

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
    BlendMode blendMode = BlendMode::None,
    bool reverseDepth = false);

} // namespace burnhope

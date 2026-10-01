#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <vector>

namespace burnhope {
namespace {

bool readSpv(const char* path, std::vector<uint32_t>& out) {
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        spdlog::error("SPIR-V missing: {}", path);
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long bytes = std::ftell(f);
    std::rewind(f);
    if (bytes <= 0 || (bytes % 4) != 0) {
        std::fclose(f);
        return false;
    }
    out.resize(static_cast<size_t>(bytes) / 4);
    const size_t n = std::fread(out.data(), 1, static_cast<size_t>(bytes), f);
    std::fclose(f);
    return n == static_cast<size_t>(bytes);
}

} // namespace

bool shaderCreate(Device& d, const ShaderCreateDesc& desc, ShaderExt& out) {
    std::vector<uint32_t> spirv;
    if (desc.path == nullptr || !readSpv(desc.path, spirv)) {
        return false;
    }

    VkShaderCreateFlagsEXT flags = desc.extraFlags;
    if (desc.stage == VK_SHADER_STAGE_MESH_BIT_EXT) {
        flags |= VK_SHADER_CREATE_NO_TASK_SHADER_BIT_EXT;
    }
    if (desc.mappingCount > 0) {
        flags |= VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT;
    }

    VkShaderDescriptorSetAndBindingMappingInfoEXT mapInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_DESCRIPTOR_SET_AND_BINDING_MAPPING_INFO_EXT,
        .mappingCount = desc.mappingCount,
        .pMappings = desc.mappings,
    };

    VkShaderCreateInfoEXT ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .pNext = desc.mappingCount > 0 ? &mapInfo : nullptr,
        .flags = flags,
        .stage = desc.stage,
        .nextStage = desc.nextStage,
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = spirv.size() * sizeof(uint32_t),
        .pCode = spirv.data(),
        .pName = "main",
    };

    const VkResult r = vkCreateShadersEXT(d.device, 1, &ci, nullptr, &out.handle);
    if (r != VK_SUCCESS) {
        spdlog::error("vkCreateShadersEXT {} failed: {}", desc.path, static_cast<int>(r));
        return false;
    }
    out.stage = desc.stage;
    return true;
}

bool shaderCreateSpvFile(
    Device& d,
    const char* path,
    VkShaderStageFlagBits stage,
    VkShaderStageFlags nextStage,
    ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = stage,
        .nextStage = nextStage,
    };
    return shaderCreate(d, desc, out);
}

void shaderDestroy(Device& d, ShaderExt& s) {
    if (s.handle != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyShaderEXT(d.device, s.handle, nullptr);
    }
    s.handle = VK_NULL_HANDLE;
}

void cmdBindCompute(VkCommandBuffer cmd, VkShaderEXT cs) {
    const VkShaderStageFlagBits stage = VK_SHADER_STAGE_COMPUTE_BIT;
    vkCmdBindShadersEXT(cmd, 1, &stage, &cs);
}

void cmdBindMeshFrag(VkCommandBuffer cmd, VkShaderEXT mesh, VkShaderEXT frag) {
    const VkShaderStageFlagBits stages[] = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
        VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
        VK_SHADER_STAGE_GEOMETRY_BIT,
        VK_SHADER_STAGE_TASK_BIT_EXT,
        VK_SHADER_STAGE_MESH_BIT_EXT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    VkShaderEXT shaders[7]{};
    shaders[5] = mesh;
    shaders[6] = frag;
    vkCmdBindShadersEXT(cmd, 7, stages, shaders);
}

void cmdSetGraphicsDynamic(
    VkCommandBuffer cmd,
    VkExtent2D extent,
    uint32_t colorAttCount,
    bool depthTest) {
    const VkViewport vp{
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewportWithCount(cmd, 1, &vp);
    vkCmdSetScissorWithCount(cmd, 1, &scissor);
    vkCmdSetRasterizerDiscardEnable(cmd, VK_FALSE);
    vkCmdSetCullMode(cmd, VK_CULL_MODE_NONE);
    vkCmdSetFrontFace(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetDepthTestEnable(cmd, depthTest ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnable(cmd, depthTest ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOp(cmd, VK_COMPARE_OP_LESS);
    vkCmdSetDepthBoundsTestEnable(cmd, VK_FALSE);
    vkCmdSetStencilTestEnable(cmd, VK_FALSE);
    vkCmdSetDepthBiasEnable(cmd, VK_FALSE);
    vkCmdSetPrimitiveRestartEnable(cmd, VK_FALSE);
    vkCmdSetRasterizationSamplesEXT(cmd, VK_SAMPLE_COUNT_1_BIT);
    const VkSampleMask sampleMask = 0xFFFFFFFF;
    vkCmdSetSampleMaskEXT(cmd, VK_SAMPLE_COUNT_1_BIT, &sampleMask);
    vkCmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);
    vkCmdSetAlphaToOneEnableEXT(cmd, VK_FALSE);
    vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);
    vkCmdSetLineWidth(cmd, 1.0f);
    vkCmdSetLogicOpEnableEXT(cmd, VK_FALSE);
    vkCmdSetPrimitiveTopology(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    VkBool32 blend[2]{VK_FALSE, VK_FALSE};
    vkCmdSetColorBlendEnableEXT(cmd, 0, colorAttCount, blend);
    VkColorBlendEquationEXT eq{};
    eq.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    eq.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    eq.colorBlendOp = VK_BLEND_OP_ADD;
    eq.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    eq.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    eq.alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorBlendEquationEXT eqs[2]{eq, eq};
    vkCmdSetColorBlendEquationEXT(cmd, 0, colorAttCount, eqs);
    const VkColorComponentFlags write = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkColorComponentFlags masks[2]{write, write};
    vkCmdSetColorWriteMaskEXT(cmd, 0, colorAttCount, masks);
}

} // namespace burnhope

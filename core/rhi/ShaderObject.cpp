#include "rhi/ShaderObject.hpp"

#include "io/File.hpp"
#include "platform/Check.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool shaderCreateCode(Device& d, const ShaderCreateDesc& desc, const void* code, uint64_t codeBytes, ShaderExt& out) {
    if (code == nullptr || codeBytes == 0 || (codeBytes % 4) != 0) {
        spdlog::error("SPIR-V size is not a multiple of 4");
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

    VkPushConstantRange pushRange{
        .stageFlags = desc.pushStages != 0 ? desc.pushStages : static_cast<VkShaderStageFlags>(desc.stage),
        .offset = 0,
        .size = desc.pushBytes,
    };
    VkShaderCreateInfoEXT ci{
        .sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT,
        .pNext = desc.mappingCount > 0 ? &mapInfo : nullptr,
        .flags = flags,
        .stage = desc.stage,
        .nextStage = desc.nextStage,
        .codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT,
        .codeSize = codeBytes,
        .pCode = code,
        .pName = desc.entry != nullptr ? desc.entry : "main",
        .pushConstantRangeCount = desc.pushBytes > 0 ? 1u : 0u,
        .pPushConstantRanges = desc.pushBytes > 0 ? &pushRange : nullptr,
    };

    const VkResult r = vkCreateShadersEXT(d.device, 1, &ci, nullptr, &out.handle);
    if (r != VK_SUCCESS) {
        out.handle = VK_NULL_HANDLE;
        spdlog::error("vkCreateShadersEXT {} failed: {}", desc.path != nullptr ? desc.path : "nrd", static_cast<int>(r));
        return false;
    }
    out.stage = desc.stage;
    return true;
}

bool shaderCreate(Device& d, const ShaderCreateDesc& desc, ShaderExt& out) {
    MappedFile mapped{};
    if (desc.path == nullptr || !fileMapReadOnly(desc.path, &mapped)) {
        spdlog::error("SPIR-V missing or cannot map: {}", desc.path != nullptr ? desc.path : "null");
        return false;
    }
    if (mapped.size == 0 || (mapped.size % 4) != 0) {
        fileUnmap(&mapped);
        spdlog::error("SPIR-V size is not a multiple of 4: {}", desc.path);
        return false;
    }
    const bool ok = shaderCreateCode(d, desc, mapped.data, mapped.size, out);
    fileUnmap(&mapped);
    return ok;
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

void cmdBindVertFrag(VkCommandBuffer cmd, VkShaderEXT vert, VkShaderEXT frag, bool nullMesh) {
    if (nullMesh) {
        const VkShaderStageFlagBits stages[] = {
            VK_SHADER_STAGE_VERTEX_BIT,
            VK_SHADER_STAGE_MESH_BIT_EXT,
            VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        const VkShaderEXT shaders[] = {vert, VK_NULL_HANDLE, frag};
        vkCmdBindShadersEXT(cmd, 3, stages, shaders);
        return;
    }
    const VkShaderStageFlagBits stages[] = {
        VK_SHADER_STAGE_VERTEX_BIT,
        VK_SHADER_STAGE_FRAGMENT_BIT,
    };
    const VkShaderEXT shaders[] = {vert, frag};
    vkCmdBindShadersEXT(cmd, 2, stages, shaders);
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
    bool depthTest,
    bool alphaBlend,
    BlendMode blendMode,
    bool reverseDepth) {
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
    vkCmdSetDepthCompareOp(cmd, reverseDepth ? VK_COMPARE_OP_GREATER_OR_EQUAL : VK_COMPARE_OP_LESS);
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

    if (colorAttCount == 0) {
        return;
    }
    BH_ASSERT(colorAttCount <= kMaxColorAttachments, "color attachment count");
    if (colorAttCount > kMaxColorAttachments) {
        return;
    }
    VkBool32 blend[kMaxColorAttachments]{};
    for (uint32_t i = 0; i < colorAttCount; ++i) {
        blend[i] = (i == 0 && (alphaBlend || blendMode != BlendMode::None)) ? VK_TRUE : VK_FALSE;
    }
    vkCmdSetColorBlendEnableEXT(cmd, 0, colorAttCount, blend);
    VkColorBlendEquationEXT eq{};
    eq.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    eq.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    if (blendMode == BlendMode::Alpha) {
        eq.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        eq.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (blendMode == BlendMode::Premultiplied) {
        eq.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        eq.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    } else if (blendMode == BlendMode::Additive) {
        eq.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        eq.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    } else if (blendMode == BlendMode::Multiply) {
        eq.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
        eq.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
    } else if (alphaBlend) {
        eq.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        eq.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }
    eq.colorBlendOp = VK_BLEND_OP_ADD;
    const bool isBlending = alphaBlend || blendMode != BlendMode::None;
    eq.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    eq.dstAlphaBlendFactor = isBlending ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
    eq.alphaBlendOp = VK_BLEND_OP_ADD;
    VkColorBlendEquationEXT eqs[kMaxColorAttachments]{};
    const VkColorComponentFlags write = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
        | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkColorComponentFlags masks[kMaxColorAttachments]{};
    for (uint32_t i = 0; i < colorAttCount; ++i) {
        eqs[i] = eq;
        masks[i] = write;
    }
    vkCmdSetColorBlendEquationEXT(cmd, 0, colorAttCount, eqs);
    vkCmdSetColorWriteMaskEXT(cmd, 0, colorAttCount, masks);
}

} // namespace burnhope

#include "render/Cube.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "render/PassShaderGroup.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool cubePassCreate(CubePass& p, Device& d, const DescriptorHeaps& heaps) {
    cubePassDestroy(p, d);
    const char* path[6] = {
        BH_SHADER_DIR "/cube0.mesh.spv",
        BH_SHADER_DIR "/cube1.mesh.spv",
        BH_SHADER_DIR "/cube2.mesh.spv",
        BH_SHADER_DIR "/cube3.mesh.spv",
        BH_SHADER_DIR "/cube4.mesh.spv",
        BH_SHADER_DIR "/cube5.mesh.spv",
    };
    VkDescriptorSetAndBindingMappingEXT maps[6][12];
    VkDescriptorSetAndBindingMappingEXT fragMaps[8];
    if (!createShaderGroup(d, p.mesh, path, VK_SHADER_STAGE_MESH_BIT_EXT, [&](uint32_t face, ShaderCreateDesc& desc) {
            PassBindings b{heaps};
            b.sun();
            b.storageBuf(7, HeapBuf::Verts);
            b.storageBuf(8, HeapBuf::Indices);
            b.storageBuf(9, HeapBuf::Meshlets);
            b.storageBuf(10, HeapBuf::Instances);
            b.storageBuf(21, cubeListSlot(face));
            for (uint32_t i = 0; i < b.raw.count; ++i) {
                maps[face][i] = b.raw.mappings[i];
            }
            desc.mappingCount = b.raw.count;
            desc.mappings = maps[face];
        }, VK_SHADER_STAGE_FRAGMENT_BIT)) {
        spdlog::error("cube mesh shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    PassBindings fragBind{heaps};
    fragBind.sun();
    fragBind.storageBuf(10, HeapBuf::Instances);
    fragBind.sampledImg(12, HeapImg::Textures);
    fragBind.sampler(13, HeapSamp::Linear);
    fragBind.storageBuf(14, HeapBuf::Materials);
    for (uint32_t i = 0; i < fragBind.raw.count; ++i) {
        fragMaps[i] = fragBind.raw.mappings[i];
    }
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/cube.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = fragBind.raw.count,
        .mappings = fragMaps,
    };
    if (!shaderCreate(d, frag, p.frag)) {
        spdlog::error("cube fragment shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    PassBindings fillBind{heaps};
    fillBind.sampler(23, HeapSamp::Linear);
    fillBind.storageImg(39, HeapImg::CubeFill);
    fillBind.sampledImg(42, HeapImg::SkyViewSample);
    fillBind.storageBuf(46, HeapBuf::CloudNoise);
    fillBind.knob(58, HeapBuf::Atm);
    const ShaderCreateDesc fillDesc{
        .path = BH_SHADER_DIR "/cube.fill.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = fillBind.raw.count,
        .mappings = fillBind.raw.mappings,
    };
    if (!shaderCreate(d, fillDesc, p.fill)) {
        spdlog::error("cube fill shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    PassBindings bakeBind{heaps};
    bakeBind.sampledImg(16, HeapImg::Cube);
    bakeBind.storageBuf(19, HeapBuf::Probes, false);
    bakeBind.sampler(24, HeapSamp::Wrap);
    const ShaderCreateDesc bake{
        .path = BH_SHADER_DIR "/probe.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bakeBind.raw.count,
        .mappings = bakeBind.raw.mappings,
    };
    if (!shaderCreate(d, bake, p.bake)) {
        spdlog::error("probe bake shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    return true;
}

void cubePassDestroy(CubePass& p, Device& d) {
    for (uint32_t face = 0; face < 6; ++face) {
        shaderDestroy(d, p.mesh[face]);
    }
    shaderDestroy(d, p.fill);
    shaderDestroy(d, p.frag);
    shaderDestroy(d, p.bake);
}

void cubePassRecord(VkCommandBuffer cmd, const CubePass& p, TrackedImage& color, TrackedImage& depth, const uint32_t counts[6], uint32_t face, bool clearColor) {
    const VkAccessFlags2 colorAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT
        | (clearColor ? VkAccessFlags2{0} : VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT);
    const VkAccessFlags2 depthAccess = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        | (clearColor ? VkAccessFlags2{0} : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
    imageBarrier(cmd, color, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, colorAccess);
    imageBarrier(cmd, depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        depthAccess, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkClearValue colorClear{};
    colorClear.color.float32[0] = 0.45f;
    colorClear.color.float32[1] = 0.55f;
    colorClear.color.float32[2] = 0.7f;
    colorClear.color.float32[3] = 0.0f;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;
    RenderingPass rendering = beginRenderingColorDepth(cmd, color.image.extent, color.image.view, depth.image.view, colorClear, depthClear, clearColor);
    cmdSetGraphicsDynamic(cmd, color.image.extent, 1, true);
    const uint32_t slot = face % 6u;
    if (counts[slot] > 0) {
        meshDrawRecord(cmd, MeshDrawDesc{
            .mesh = p.mesh[slot].handle,
            .frag = p.frag.handle,
            .groupCount = counts[slot],
        });
    }
    endRendering(rendering);
    imageBarrier(cmd, color, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    dispatchCompute2D(cmd, p.fill, color.image.extent, 8u);
    imageBarrier(cmd, color, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
}

void probeBakeRecord(VkCommandBuffer cmd, const CubePass& p, VkBuffer probes, VkDeviceSize probeBytes) {
    if (p.bake.handle == VK_NULL_HANDLE || probes == VK_NULL_HANDLE) {
        return;
    }
    rhiBufferBarrier(cmd, probes, 0, probeBytes,
        VK_PIPELINE_STAGE_2_HOST_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    cmdBindCompute(cmd, p.bake.handle);
    vkCmdDispatch(cmd, 1, 1, 1);
    rhiBufferBarrier(cmd, probes, 0, probeBytes,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

} // namespace burnhope

#include "render/Cube.hpp"

#include "render/RenderDefs.hpp"
#include "render/HeapBind.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool cubePassCreate(CubePass& p, Device& d, const DescriptorHeaps& heaps) {
    cubePassDestroy(p, d);
    PassBindings meshBind{heaps};
    meshBind.sun();
    meshBind.storageBuf(7, HeapBuf::Verts);
    meshBind.storageBuf(8, HeapBuf::Indices);
    meshBind.storageBuf(9, HeapBuf::Meshlets);
    meshBind.storageBuf(10, HeapBuf::Instances);
    meshBind.storageBuf(21, HeapBuf::CubeVisible);
    const ShaderCreateDesc meshDesc{
        .path = BH_SHADER_DIR "/cube.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = meshBind.raw.count,
        .mappings = meshBind.raw.mappings,
        .pushBytes = 16,
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    };
    if (!shaderCreate(d, meshDesc, p.mesh)) {
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
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/cube.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = fragBind.raw.count,
        .mappings = fragBind.raw.mappings,
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
    if (!createComputeShader(d, BH_SHADER_DIR "/cube.fill.comp.spv", fillBind, p.fill)) {
        spdlog::error("cube fill shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    PassBindings bakeBind{heaps};
    bakeBind.sampledImg(16, HeapImg::Cube);
    bakeBind.storageBuf(19, HeapBuf::Probes, false);
    bakeBind.sampler(24, HeapSamp::Wrap);
    if (!createComputeShader(d, BH_SHADER_DIR "/probe.comp.spv", bakeBind, p.bake)) {
        spdlog::error("probe bake shader failed");
        cubePassDestroy(p, d);
        return false;
    }
    return true;
}

void cubePassDestroy(CubePass& p, Device& d) {
    shaderDestroy(d, p.mesh);
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
    const uint32_t side = color.image.extent.height;
    if (!clearColor && side > 0u) {
        VkClearAttachment clearAtt[2]{};
        clearAtt[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clearAtt[0].colorAttachment = 0;
        clearAtt[0].clearValue = colorClear;
        clearAtt[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clearAtt[1].clearValue = depthClear;
        VkClearRect clearRect{};
        clearRect.rect.offset.x = static_cast<int32_t>(slot * side);
        clearRect.rect.extent = {side, side};
        clearRect.layerCount = 1;
        vkCmdClearAttachments(cmd, 2, clearAtt, 1, &clearRect);
        vkCmdSetScissorWithCount(cmd, 1, &clearRect.rect);
    }
    if (counts[slot] > 0) {
        struct CubePush {
            uint32_t face;
            uint32_t stride;
            uint32_t pad0;
            uint32_t pad1;
        };
        const CubePush push{slot, p.listStride, 0, 0};
        meshDrawRecord(cmd, MeshDrawDesc{
            .mesh = p.mesh.handle,
            .frag = p.frag.handle,
            .groupCount = counts[slot],
            .pushData = &push,
            .pushBytes = sizeof(push),
            .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
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

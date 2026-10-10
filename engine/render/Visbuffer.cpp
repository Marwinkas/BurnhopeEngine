#include "render/Visbuffer.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"
#include "gpu_scene/InstanceData.hpp"
#include "render/PassTargets.hpp"
#include "render/PassRendering.hpp"
#include "render/PassShaderGroup.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

bool visTargetsCreate(VisTargets& t, Device& d, VkExtent2D extent) {
    const bool visOk = recreateTracked(
        t.vis, d, extent, VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    const bool depthOk = recreateTracked(
        t.depth, d, extent, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    return visOk && depthOk;
}

void visTargetsDestroy(VisTargets& t, Device& d) {
    destroyTracked(d, {&t.vis, &t.depth});
}

void bindVisMesh(PassBindings& b) {
    b.ubo(0, HeapBuf::Frame);
    b.storageBuf(7, HeapBuf::Verts);
    b.storageBuf(8, HeapBuf::Indices);
    b.storageBuf(9, HeapBuf::Meshlets);
    b.storageBuf(10, HeapBuf::Instances);
    b.storageBuf(11, HeapBuf::Visible);
}

void copyBindings(const PassBindings& b, VkDescriptorSetAndBindingMappingEXT* dst) {
    for (uint32_t i = 0; i < b.raw.count; ++i) {
        dst[i] = b.raw.mappings[i];
    }
}

} // namespace

bool visPassCreate(VisPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    visPassDestroy(p, d);
    if (!visTargetsCreate(p.targets, d, extent)) {
        return false;
    }
    PassBindings mesh{heaps};
    bindVisMesh(mesh);
    VkDescriptorSetAndBindingMappingEXT maps[6];
    copyBindings(mesh, maps);
    const char* meshPath[2] = {BH_SHADER_DIR "/visbuffer.mesh.spv", BH_SHADER_DIR "/visbuffer.mesh1.spv"};
    const char* latePath[2] = {BH_SHADER_DIR "/visbuffer.late.mesh.spv", BH_SHADER_DIR "/visbuffer.late1.mesh.spv"};
    const char* shadowPath[3] = {
        BH_SHADER_DIR "/visbuffer.shadow0.mesh.spv",
        BH_SHADER_DIR "/visbuffer.shadow1.mesh.spv",
        BH_SHADER_DIR "/visbuffer.shadow2.mesh.spv",
    };
    const char* pointPath[3] = {
        BH_SHADER_DIR "/visbuffer.point0.mesh.spv",
        BH_SHADER_DIR "/visbuffer.point1.mesh.spv",
        BH_SHADER_DIR "/visbuffer.point2.mesh.spv",
    };
    VkDescriptorSetAndBindingMappingEXT shadowMaps[3][8];
    VkDescriptorSetAndBindingMappingEXT pointMaps[3][8];
    VkDescriptorSetAndBindingMappingEXT fragMaps[9];
    PassBindings fragBind{heaps};
    bindVisMesh(fragBind);
    fragBind.sampledImg(12, HeapImg::Textures);
    fragBind.storageBuf(14, HeapBuf::Materials);
    fragBind.sampler(24, HeapSamp::Wrap);
    copyBindings(fragBind, fragMaps);
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/visbuffer.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 9,
        .mappings = fragMaps,
    };
    const bool shaders = createShaderArray(d, p.mesh, meshPath, VK_SHADER_STAGE_MESH_BIT_EXT, maps, 6u, VK_SHADER_STAGE_FRAGMENT_BIT)
        && createShaderArray(d, p.meshLate, latePath, VK_SHADER_STAGE_MESH_BIT_EXT, maps, 6u, VK_SHADER_STAGE_FRAGMENT_BIT)
        && createShaderGroup(d, p.shadow, shadowPath, VK_SHADER_STAGE_MESH_BIT_EXT, [&](uint32_t cascade, ShaderCreateDesc& desc) {
            PassBindings shadowBind{heaps};
            shadowBind.ubo(0, HeapBuf::Frame);
            shadowBind.storageBuf(7, HeapBuf::Verts);
            shadowBind.storageBuf(8, HeapBuf::Indices);
            shadowBind.storageBuf(9, HeapBuf::Meshlets);
            shadowBind.storageBuf(10, HeapBuf::Instances);
            shadowBind.sun();
            shadowBind.storageBuf(18, shadowListSlot(cascade));
            copyBindings(shadowBind, shadowMaps[cascade]);
            desc.mappingCount = 7;
            desc.mappings = shadowMaps[cascade];
        }, VK_SHADER_STAGE_FRAGMENT_BIT)
        && createShaderGroup(d, p.point, pointPath, VK_SHADER_STAGE_MESH_BIT_EXT, [&](uint32_t light, ShaderCreateDesc& desc) {
            PassBindings pointBind{heaps};
            pointBind.ubo(0, HeapBuf::Frame);
            pointBind.storageBuf(7, HeapBuf::Verts);
            pointBind.storageBuf(8, HeapBuf::Indices);
            pointBind.storageBuf(9, HeapBuf::Meshlets);
            pointBind.storageBuf(10, HeapBuf::Instances);
            pointBind.sun();
            pointBind.storageBuf(19, static_cast<uint32_t>(HeapBuf::PointVisible) + light);
            copyBindings(pointBind, pointMaps[light]);
            desc.mappingCount = 7;
            desc.mappings = pointMaps[light];
            desc.nextStage = VK_SHADER_STAGE_FRAGMENT_BIT;
        }, VK_SHADER_STAGE_FRAGMENT_BIT)
        && shaderCreate(d, ShaderCreateDesc{
            .path = BH_SHADER_DIR "/visbuffer.shadow.frag.spv",
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .extraFlags = VK_SHADER_CREATE_DESCRIPTOR_HEAP_BIT_EXT,
        }, p.shadowFrag)
        && shaderCreate(d, frag, p.frag);
    if (!shaders) {
        spdlog::error("visPass shaders failed");
        visPassDestroy(p, d);
        return false;
    }
    return true;
}

void visPassDestroy(VisPass& p, Device& d) {
    if (p.pushLayout != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(d.device, p.pushLayout, nullptr);
        p.pushLayout = VK_NULL_HANDLE;
    }
    shaderDestroy(d, p.mesh[0]);
    shaderDestroy(d, p.mesh[1]);
    shaderDestroy(d, p.meshLate[0]);
    shaderDestroy(d, p.meshLate[1]);
    shaderDestroy(d, p.shadow[0]);
    shaderDestroy(d, p.shadow[1]);
    shaderDestroy(d, p.shadow[2]);
    shaderDestroy(d, p.shadowFrag);
    shaderDestroy(d, p.point[0]);
    shaderDestroy(d, p.point[1]);
    shaderDestroy(d, p.point[2]);
    shaderDestroy(d, p.frag);
    visTargetsDestroy(p.targets, d);
}

bool visPassResize(VisPass& p, Device& d, VkExtent2D extent) {
    return visTargetsCreate(p.targets, d, extent);
}

void visPassRecord(VkCommandBuffer cmd, VkExtent2D extent, VisPass& p, const VisInstanceDraw& draw, bool load, uint32_t flight) {
    VisTargets& t = p.targets;
    const VkAccessFlags2 colorDst = load
        ? (VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT)
        : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    const VkAccessFlags2 depthDst = load
        ? (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
        : VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    imageBarrier(cmd, t.vis, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, colorDst);
    imageBarrier(cmd, t.depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, depthDst, VK_IMAGE_ASPECT_DEPTH_BIT);

    VkClearValue visClear{};
    visClear.color.uint32[0] = 0;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 0.0f;
    RenderingPass rendering = beginRenderingColorDepth(cmd, extent, t.vis.image.view, t.depth.image.view, visClear, depthClear, !load);
    cmdSetGraphicsDynamic(cmd, extent, 1, true, false, 0, true);
    // Same rhi/MeshDraw entry point as core/ui/Canvas.cpp — one registered draw per pass
    // (plan Фаза 1/5), no second mesh-shader dispatch path for engine/.
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = load ? p.meshLate[flight & 1u].handle : p.mesh[flight & 1u].handle,
        .frag = p.frag.handle,
        .groupCount = draw.visibleCount,
        .indirect = draw.indirect,
        .indirectOffset = draw.indirectOffset,
    });
    endRendering(rendering);
}

void visShadowRecord(VkCommandBuffer cmd, const VisPass& p, TrackedImage& depth, const VisInstanceDraw& draw, uint32_t cascade) {
    const VkExtent2D extent = depth.image.extent;
    const bool fresh = depth.layout == VK_IMAGE_LAYOUT_UNDEFINED;
    const VkAccessFlags2 dst = fresh
        ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
        : (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    imageBarrier(cmd, depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, dst, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 0.0f;
    const uint32_t band = extent.width > 0 ? extent.width : 1u;
    const int32_t bandY = static_cast<int32_t>(cascade * band);
    const VkRect2D bandRect{{0, bandY}, {band, band}};
    const VkRect2D fullRect{{0, 0}, {extent.width, extent.height}};
    RenderingPass rendering = beginRenderingDepthOnly(cmd, extent, depth.image.view, depthClear, fresh, fresh ? fullRect : bandRect);
    if (!fresh) {
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clear.clearValue.depthStencil.depth = 0.0f;
        VkClearRect clearRect{bandRect, 0u, 1u};
        vkCmdClearAttachments(cmd, 1, &clear, 1, &clearRect);
    }
    cmdSetGraphicsDynamic(cmd, extent, 0, true, false, 0, true);
    const VkViewport vp{0.0f, static_cast<float>(bandY), static_cast<float>(band), static_cast<float>(band), 0.0f, 1.0f};
    const VkRect2D scissor = bandRect;
    vkCmdSetViewportWithCount(cmd, 1, &vp);
    vkCmdSetScissorWithCount(cmd, 1, &scissor);
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = p.shadow[cascade].handle,
        .frag = p.shadowFrag.handle,
        .groupCount = draw.visibleCount,
        .indirect = draw.indirect,
        .indirectOffset = draw.indirectOffset,
    });
    endRendering(rendering);
}

void visPointRecord(VkCommandBuffer cmd, const VisPass& p, TrackedImage& depth, const uint32_t counts[3]) {
    imageBarrier(cmd, depth, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;
    RenderingPass rendering = beginRenderingDepthOnly(cmd, depth.image.extent, depth.image.view, depthClear, true);
    cmdSetGraphicsDynamic(cmd, depth.image.extent, 1, true);
    for (uint32_t light = 0; light < 3; ++light) {
        if (counts[light] == 0) {
            continue;
        }
        meshDrawRecord(cmd, MeshDrawDesc{
            .mesh = p.point[light].handle,
            .frag = VK_NULL_HANDLE,
            .groupCount = counts[light],
        });
    }
    endRendering(rendering);
    imageBarrier(cmd, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

} // namespace burnhope

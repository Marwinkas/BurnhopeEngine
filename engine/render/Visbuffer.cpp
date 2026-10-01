#include "render/Visbuffer.hpp"
#include "render/HeapMaps.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

bool visTargetsCreate(VisTargets& t, Device& d, VkExtent2D extent) {
    const bool visOk = gpuImageCreate(
        t.vis, d, extent, VK_FORMAT_R32_UINT,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    const bool depthOk = gpuImageCreate(
        t.depth, d, extent, VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);
    return visOk && depthOk;
}

void visTargetsDestroy(VisTargets& t, Device& d) {
    gpuImageDestroy(t.vis, d);
    gpuImageDestroy(t.depth, d);
}

} // namespace

bool visPassCreate(VisPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent) {
    visPassDestroy(p, d);
    if (!visTargetsCreate(p.targets, d, extent)) {
        return false;
    }
    VkDescriptorSetAndBindingMappingEXT maps[2];
    heapMapsVis(heaps, maps);
    const ShaderCreateDesc mesh{
        .path = "shaders/visbuffer.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 2,
        .mappings = maps,
    };
    const ShaderCreateDesc frag{
        .path = "shaders/visbuffer.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 2,
        .mappings = maps,
    };
    if (!shaderCreate(d, mesh, p.mesh) || !shaderCreate(d, frag, p.frag)) {
        spdlog::error("visPass shaders failed");
        visPassDestroy(p, d);
        return false;
    }
    return true;
}

void visPassDestroy(VisPass& p, Device& d) {
    shaderDestroy(d, p.mesh);
    shaderDestroy(d, p.frag);
    visTargetsDestroy(p.targets, d);
}

bool visPassResize(VisPass& p, Device& d, VkExtent2D extent) {
    return visTargetsCreate(p.targets, d, extent);
}

void visPassRecord(VkCommandBuffer cmd, VkExtent2D extent, const VisPass& p) {
    const VisTargets& t = p.targets;
    rhiImageBarrier(cmd, t.vis.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    rhiImageBarrier(cmd, t.depth.image,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_DEPTH_BIT);

    VkClearValue visClear{};
    visClear.color.uint32[0] = 0;
    VkClearValue depthClear{};
    depthClear.depthStencil.depth = 1.0f;

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = t.vis.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = visClear;

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = t.depth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue = depthClear;

    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
        .pDepthAttachment = &depth,
    };
    vkCmdBeginRendering(cmd, &ri);
    cmdBindMeshFrag(cmd, p.mesh.handle, p.frag.handle);
    cmdSetGraphicsDynamic(cmd, extent, 1, true);
    vkCmdDrawMeshTasksEXT(cmd, 1, 1, 1);
    vkCmdEndRendering(cmd);
}

} // namespace burnhope

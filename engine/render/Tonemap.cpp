#include "render/Tonemap.hpp"
#include "render/HeapMaps.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/MeshDraw.hpp"
#include "rhi/ShaderObject.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

bool tonemapPassCreate(TonemapPass& p, Device& d, const DescriptorHeaps& heaps) {
    tonemapPassDestroy(p, d);
    VkDescriptorSetAndBindingMappingEXT maps[2];
    heapMapsTonemap(heaps, maps);
    const ShaderCreateDesc mesh{
        .path = "shaders/tonemap.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 2,
        .mappings = maps,
    };
    const ShaderCreateDesc frag{
        .path = "shaders/tonemap.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = 2,
        .mappings = maps,
    };
    if (!shaderCreate(d, mesh, p.mesh) || !shaderCreate(d, frag, p.frag)) {
        spdlog::error("tonemap shaders failed");
        tonemapPassDestroy(p, d);
        return false;
    }
    return true;
}

void tonemapPassDestroy(TonemapPass& p, Device& d) {
    shaderDestroy(d, p.mesh);
    shaderDestroy(d, p.frag);
}

void tonemapPassRecord(
    VkCommandBuffer cmd,
    const Swapchain& sc,
    const FrameContext& fc,
    const TonemapPass& p) {
    rhiImageBarrier(cmd, sc.images[fc.imageIndex],
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkClearValue clear{};
    clear.color.float32[3] = 1.0f;
    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = sc.views[fc.imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;

    VkRenderingInfo ri{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {{0, 0}, sc.extent},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &color,
    };
    vkCmdBeginRendering(cmd, &ri);
    cmdSetGraphicsDynamic(cmd, sc.extent, 1, false);
    meshDrawRecord(cmd, MeshDrawDesc{.mesh = p.mesh.handle, .frag = p.frag.handle, .groupCount = 1});
    vkCmdEndRendering(cmd);
}

} // namespace burnhope

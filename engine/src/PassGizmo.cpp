#include "render/Gizmo.hpp"

#include "render/RenderDefs.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassRendering.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/MeshDraw.hpp"

#include <cstring>

namespace burnhope {

bool gizmoPassCreate(GizmoPass& pass, Device& device, DescriptorHeaps& heaps) {
    gizmoPassDestroy(pass, device);
    if (!device.caps.meshShader) {
        return false;
    }
    if (!createDefaultGpuBuffer(pass.buf, device, sizeof(GizmoGpu) * 2)) {
        return false;
    }
    PassBindings bind{heaps};
    bind.ubo(47, HeapBuf::Gizmo);
    const ShaderCreateDesc mesh{
        .path = BH_SHADER_DIR "/gizmo.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
        .pushBytes = 16,
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    };
    const ShaderCreateDesc frag{
        .path = BH_SHADER_DIR "/gizmo.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
    };
    if (!shaderCreate(device, mesh, pass.mesh) || !shaderCreate(device, frag, pass.frag)) {
        gizmoPassDestroy(pass, device);
        return false;
    }
    const bool bound =
        heapWriteBufferFlight(heaps, device, 0, 47, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, pass.buf.address, sizeof(GizmoGpu))
        && heapWriteBufferFlight(heaps, device, 1, 47, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, pass.buf.address + sizeof(GizmoGpu), sizeof(GizmoGpu));
    if (!bound) {
        gizmoPassDestroy(pass, device);
        return false;
    }
    return true;
}

void gizmoPassDestroy(GizmoPass& pass, Device& device) {
    shaderDestroy(device, pass.mesh);
    shaderDestroy(device, pass.frag);
    gpuBufferDestroy(pass.buf, device);
    gpuImageDestroy(pass.depth[0].image, device);
    gpuImageDestroy(pass.depth[1].image, device);
    pass.depth[0] = {};
    pass.depth[1] = {};
}

void gizmoPassRecord(VkCommandBuffer cmd, Device& device, const Swapchain& swap, const FrameContext& frame, GizmoPass& pass, const SceneActors& actors) {
    if (pass.mesh.handle == VK_NULL_HANDLE || pass.buf.mapped == nullptr || frame.imageIndex >= 8) {
        return;
    }
    const uint32_t flight = frame.flight & 1u;
    GizmoGpu gpu{};
    sceneActorFillGpu(actors, gpu);
    std::memcpy(static_cast<std::byte*>(pass.buf.mapped) + sizeof(GizmoGpu) * flight, &gpu, sizeof(gpu));
    rhiBufferBarrier(
        cmd,
        pass.buf.buffer,
        sizeof(GizmoGpu) * flight,
        sizeof(GizmoGpu),
        VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_MESH_SHADER_BIT_EXT,
        VK_ACCESS_2_UNIFORM_READ_BIT);
    TrackedImage& depthImg = pass.depth[flight];
    const bool depthOk = depthImg.image.extent.width == swap.extent.width && depthImg.image.extent.height == swap.extent.height
        && depthImg.image.image != VK_NULL_HANDLE;
    if (!depthOk) {
        gpuImageDestroy(depthImg.image, device);
        depthImg = {};
        if (!gpuImageCreate(
                depthImg.image,
                device,
                swap.extent,
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT)) {
            depthImg = {};
        }
    }
    const bool depth = depthImg.image.view != VK_NULL_HANDLE;
    if (depth) {
        imageBarrier(
            cmd,
            depthImg,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            true);
    }
    RenderingPass rendering;
    rendering.cmd = cmd;
    VkClearValue colorClear{};
    fillAttachment(rendering.color, swap.views[frame.imageIndex], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ATTACHMENT_LOAD_OP_LOAD, colorClear);
    if (depth) {
        VkClearValue depthClear{};
        depthClear.depthStencil.depth = 0.0f;
        fillAttachment(
            rendering.depth,
            depthImg.image.view,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_ATTACHMENT_LOAD_OP_CLEAR,
            depthClear);
    }
    rendering.info = {};
    rendering.info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.info.renderArea = {{0, 0}, swap.extent};
    rendering.info.layerCount = 1;
    rendering.info.colorAttachmentCount = 1;
    rendering.retarget();
    vkCmdBeginRendering(cmd, &rendering.info);
    rendering.open = true;
    struct GizmoPhase {
        uint32_t phase = 0;
        uint32_t pad0 = 0;
        uint32_t pad1 = 0;
        uint32_t pad2 = 0;
    };
    GizmoPhase phase{};
    cmdSetGraphicsDynamic(cmd, swap.extent, 1, depth, false, BlendMode::None, true);
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = pass.mesh.handle,
        .frag = pass.frag.handle,
        .groupCount = 11,
        .pushData = &phase,
        .pushBytes = 16,
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    });
    phase.phase = 1;
    cmdSetGraphicsDynamic(cmd, swap.extent, 1, false, false, BlendMode::None, true);
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = pass.mesh.handle,
        .frag = pass.frag.handle,
        .groupCount = 11,
        .pushData = &phase,
        .pushBytes = 16,
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    });
    endRendering(rendering);
}

} // namespace burnhope

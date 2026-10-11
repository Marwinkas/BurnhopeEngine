#include "render/Gizmo.hpp"

#ifndef BH_SHADER_DIR
#define BH_SHADER_DIR "shaders"
#endif
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassRendering.hpp"
#include "rhi/Barrier.hpp"
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
}

void gizmoPassRecord(VkCommandBuffer cmd, const Swapchain& swap, const FrameContext& frame, GizmoPass& pass, const SceneActors& actors) {
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
    VkClearValue clear{};
    RenderingPass rendering = beginRenderingColorDepth(cmd, swap.extent, swap.views[frame.imageIndex], VK_NULL_HANDLE, clear, {}, false);
    cmdSetGraphicsDynamic(cmd, swap.extent, 1, false);
    meshDrawRecord(cmd, MeshDrawDesc{.mesh = pass.mesh.handle, .frag = pass.frag.handle, .groupCount = 1});
    endRendering(rendering);
}

} // namespace burnhope

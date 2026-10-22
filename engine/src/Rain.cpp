#include "render/Rain.hpp"

#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/MeshDraw.hpp"

#include <cstring>

namespace burnhope {

bool rainCreate(RainPass& pass, Device& device, DescriptorHeaps& heaps) {
    rainDestroy(pass, device);
    if (!device.caps.meshShader) {
        return false;
    }
    if (!createDefaultGpuBuffer(pass.drops, device, kRainSlice * 2u) || pass.drops.mapped == nullptr) {
        rainDestroy(pass, device);
        return false;
    }
    std::memset(pass.drops.mapped, 0, pass.drops.size);
    PassBindings sim{heaps};
    sim.ubo(0, HeapBuf::Frame);
    sim.sampledImg(2, HeapImg::Depth);
    sim.storageBuf(kRainSlot, kRainSlot, false);
    sim.knob(58, HeapBuf::Atm);
    PassBindings mesh{heaps};
    mesh.ubo(0, HeapBuf::Frame);
    mesh.storageBuf(kRainSlot, kRainSlot, false);
    PassBindings frag{heaps};
    frag.ubo(0, HeapBuf::Frame);
    frag.sun();
    frag.sampledImg(2, HeapImg::Depth);
    const ShaderCreateDesc simDesc{
        .path = BH_SHADER_DIR "/rain.sim.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = sim.raw.count,
        .mappings = sim.raw.mappings,
        .pushBytes = sizeof(RainPush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const ShaderCreateDesc meshDesc{
        .path = BH_SHADER_DIR "/rain.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = mesh.raw.count,
        .mappings = mesh.raw.mappings,
        .pushBytes = sizeof(RainPush),
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    };
    const ShaderCreateDesc fragDesc{
        .path = BH_SHADER_DIR "/rain.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = frag.raw.count,
        .mappings = frag.raw.mappings,
    };
    if (!shaderCreate(device, simDesc, pass.sim) || !shaderCreate(device, meshDesc, pass.mesh) || !shaderCreate(device, fragDesc, pass.frag)) {
        rainDestroy(pass, device);
        return false;
    }
    if (!heapWriteStorage(heaps, device, kRainSlot, pass.drops.address, pass.drops.size)) {
        rainDestroy(pass, device);
        return false;
    }
    return true;
}

void rainDestroy(RainPass& pass, Device& device) {
    shaderDestroy(device, pass.sim);
    shaderDestroy(device, pass.mesh);
    shaderDestroy(device, pass.frag);
    gpuBufferDestroy(pass.drops, device);
    pass.gpuLive = 0;
}

void rainRecord(
    VkCommandBuffer cmd,
    RainPass& pass,
    TrackedImage& hdr,
    VkExtent2D extent,
    uint32_t flight,
    float dt) {
    if (pass.sim.handle == VK_NULL_HANDLE || pass.mesh.handle == VK_NULL_HANDLE || hdr.image.view == VK_NULL_HANDLE) {
        return;
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    const uint32_t slice = flight & 1u;
    if (pass.gpuLive == 0) {
        bufferBarrierHostToShader(
            cmd, pass.drops.buffer, pass.drops.size,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        pass.gpuLive = 1;
    }
    cmdBindCompute(cmd, pass.sim.handle);
    const RainPush simPush{slice, 0u, dt, 0u};
    vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(simPush), &simPush);
    vkCmdDispatch(cmd, (kRainCap + 63u) / 64u, 1u, 1u);
    bufferBarrierComputeToMeshShader(cmd, pass.drops.buffer, pass.drops.size);
    imageBarrier(
        cmd, hdr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
    VkClearValue clear{};
    RenderingPass rendering = beginRenderingColorDepth(cmd, extent, hdr.image.view, VK_NULL_HANDLE, clear, clear, false);
    cmdSetGraphicsDynamic(cmd, extent, 1, false, false, BlendMode::Premultiplied);
    const RainPush drawPush{slice, 0u, dt, 0u};
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = pass.mesh.handle,
        .frag = pass.frag.handle,
        .groupCount = kRainGroups,
        .pushData = &drawPush,
        .pushBytes = sizeof(drawPush),
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    });
    endRendering(rendering);
}

} // namespace burnhope

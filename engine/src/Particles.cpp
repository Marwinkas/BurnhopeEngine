#include "render/Particles.hpp"

#include "gfx/Light.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/PassRendering.hpp"
#include "render/RenderDefs.hpp"
#include "rhi/MeshDraw.hpp"

#include <cstring>

namespace burnhope {

namespace {

void pushMode(VkCommandBuffer cmd, uint32_t flight, uint32_t mode, float dt) {
    const ParticlePush push{flight & 1u, mode, dt, 0};
    vkCmdPushConstants(cmd, VK_NULL_HANDLE, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
}

} // namespace

bool particlePassCreate(ParticlePass& pass, Device& device, DescriptorHeaps& heaps) {
    particlePassDestroy(pass, device);
    if (!device.caps.meshShader) {
        return false;
    }
    const bool buffers = createDefaultGpuBuffer(pass.particles, device, kParticleSlice * 2u)
        && createDefaultGpuBuffer(pass.spawn, device, sizeof(ParticleSpawn) * 2u)
        && createDefaultGpuBuffer(pass.indirect, device, 256u, true, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT);
    if (!buffers || pass.particles.mapped == nullptr || pass.spawn.mapped == nullptr || pass.indirect.mapped == nullptr) {
        particlePassDestroy(pass, device);
        return false;
    }
    std::memset(pass.particles.mapped, 0, pass.particles.size);
    std::memset(pass.spawn.mapped, 0, pass.spawn.size);
    std::memset(pass.indirect.mapped, 0, pass.indirect.size);
    PassBindings sim{heaps};
    sim.ubo(0, HeapBuf::Frame);
    sim.sun();
    sim.sampledImg(2, HeapImg::Depth);
    sim.storageBuf(kParticleSlot, kParticleSlot, false);
    sim.storageBuf(kParticleSpawnSlot, kParticleSpawnSlot, true);
    sim.storageBuf(kParticleIndirectSlot, kParticleIndirectSlot, false);
    PassBindings mesh{heaps};
    mesh.ubo(0, HeapBuf::Frame);
    mesh.storageBuf(kParticleSlot, kParticleSlot, false);
    PassBindings frag{heaps};
    frag.ubo(0, HeapBuf::Frame);
    frag.sun();
    frag.sampledImg(2, HeapImg::Depth);
    const ShaderCreateDesc simDesc{
        .path = BH_SHADER_DIR "/particle.sim.comp.spv",
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = sim.raw.count,
        .mappings = sim.raw.mappings,
        .pushBytes = sizeof(ParticlePush),
        .pushStages = VK_SHADER_STAGE_COMPUTE_BIT,
    };
    const ShaderCreateDesc meshDesc{
        .path = BH_SHADER_DIR "/particle.mesh.spv",
        .stage = VK_SHADER_STAGE_MESH_BIT_EXT,
        .nextStage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = mesh.raw.count,
        .mappings = mesh.raw.mappings,
        .pushBytes = sizeof(ParticlePush),
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    };
    const ShaderCreateDesc fragDesc{
        .path = BH_SHADER_DIR "/particle.frag.spv",
        .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
        .mappingCount = frag.raw.count,
        .mappings = frag.raw.mappings,
    };
    if (!shaderCreate(device, simDesc, pass.sim) || !shaderCreate(device, meshDesc, pass.mesh) || !shaderCreate(device, fragDesc, pass.frag)) {
        particlePassDestroy(pass, device);
        return false;
    }
    const bool bound = heapWriteStorage(heaps, device, kParticleSlot, pass.particles.address, pass.particles.size)
        && heapWriteStorage(heaps, device, kParticleSpawnSlot, pass.spawn.address, pass.spawn.size)
        && heapWriteStorage(heaps, device, kParticleIndirectSlot, pass.indirect.address, pass.indirect.size);
    if (!bound) {
        particlePassDestroy(pass, device);
        return false;
    }
    return true;
}

void particlePassDestroy(ParticlePass& pass, Device& device) {
    shaderDestroy(device, pass.sim);
    shaderDestroy(device, pass.mesh);
    shaderDestroy(device, pass.frag);
    gpuBufferDestroy(pass.particles, device);
    gpuBufferDestroy(pass.spawn, device);
    gpuBufferDestroy(pass.indirect, device);
    pass.queueCount = 0;
    pass.queueTotal = 0;
    pass.flashCount = 0;
    pass.gpuLive = 0;
}

void particleBurst(ParticlePass& pass, float x, float y, float z) {
    if (pass.queueCount >= kParticleBurstCap) {
        return;
    }
    ParticleBurst& burst = pass.queue[pass.queueCount];
    burst.pos[0] = x;
    burst.pos[1] = y;
    burst.pos[2] = z;
    burst.count = 160.0f;
    burst.seed = static_cast<float>(pass.seed);
    pass.seed += 1u;
    pass.queueCount += 1u;
    pass.queueTotal += 160u;
    if (pass.flashCount >= kParticleFlashCap) {
        for (uint32_t i = 1; i < kParticleFlashCap; ++i) {
            pass.flash[i - 1u] = pass.flash[i];
        }
        pass.flashCount = kParticleFlashCap - 1u;
    }
    ParticleFlash& flash = pass.flash[pass.flashCount];
    flash.pos[0] = x;
    flash.pos[1] = y;
    flash.pos[2] = z;
    flash.candela = 1200000.0f;
    flash.radius = 18.0f;
    flash.life = 0.45f;
    flash.maxLife = 0.45f;
    pass.flashCount += 1u;
}

void particleTick(ParticlePass& pass, float dt) {
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < pass.flashCount; ++i) {
        pass.flash[i].life -= dt;
        if (pass.flash[i].life > 0.0f) {
            pass.flash[kept] = pass.flash[i];
            kept += 1u;
        }
    }
    pass.flashCount = kept;
}

void particleRecord(
    VkCommandBuffer cmd,
    Device& device,
    ParticlePass& pass,
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
    if (pass.spawn.mapped != nullptr) {
        ParticleSpawn block{};
        block.count = pass.queueCount;
        block.total = pass.queueTotal;
        for (uint32_t i = 0; i < pass.queueCount && i < kParticleBurstCap; ++i) {
            block.burst[i] = pass.queue[i];
        }
        std::memcpy(static_cast<std::byte*>(pass.spawn.mapped) + sizeof(ParticleSpawn) * slice, &block, sizeof(block));
        gpuBufferFlush(pass.spawn, device, sizeof(ParticleSpawn) * slice, sizeof(ParticleSpawn));
        pass.queueCount = 0;
        pass.queueTotal = 0;
    }
    bufferBarrierHostToShader(
        cmd, pass.spawn.buffer, pass.spawn.size,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
    if (pass.gpuLive == 0) {
        bufferBarrierHostToShader(
            cmd, pass.particles.buffer, pass.particles.size,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        pass.gpuLive = 1;
    }
    cmdBindCompute(cmd, pass.sim.handle);
    pushMode(cmd, slice, 0u, dt);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    bufferBarrierComputeWriteToReadWrite(cmd, pass.particles.buffer, pass.particles.size);
    pushMode(cmd, slice, 1u, dt);
    vkCmdDispatch(cmd, (kParticleCap + 63u) / 64u, 1u, 1u);
    bufferBarrierComputeWriteToReadWrite(cmd, pass.particles.buffer, pass.particles.size);
    pushMode(cmd, slice, 2u, dt);
    vkCmdDispatch(cmd, 1u, 1u, 1u);
    bufferBarrierComputeToIndirect(cmd, pass.indirect.buffer, pass.indirect.size);
    bufferBarrierComputeToMeshShader(cmd, pass.particles.buffer, pass.particles.size);
    imageBarrier(
        cmd, hdr, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    VkClearValue clear{};
    RenderingPass rendering = beginRenderingColorDepth(cmd, extent, hdr.image.view, VK_NULL_HANDLE, clear, clear, false);
    cmdSetGraphicsDynamic(cmd, extent, 1, false, false, BlendMode::Premultiplied);
    const ParticlePush drawPush{slice, 0u, dt, 0u};
    meshDrawRecord(cmd, MeshDrawDesc{
        .mesh = pass.mesh.handle,
        .frag = pass.frag.handle,
        .indirect = pass.indirect.buffer,
        .indirectOffset = sizeof(uint32_t) * 4u * slice,
        .pushData = &drawPush,
        .pushBytes = sizeof(drawPush),
        .pushStages = VK_SHADER_STAGE_MESH_BIT_EXT,
    });
    endRendering(rendering);
}

} // namespace burnhope

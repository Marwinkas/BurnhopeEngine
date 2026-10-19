#include "render/Sharc.hpp"

#include "gfx/Light.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "rhi/DescriptorHeap.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr VkDeviceSize kParamStride = 256;

bool sharcShader(Device& d, const char* path, const PassBindings& bind, ShaderExt& out) {
    const ShaderCreateDesc desc{
        .path = path,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .mappingCount = bind.raw.count,
        .mappings = bind.raw.mappings,
    };
    return shaderCreate(d, desc, out);
}

void bindCache(PassBindings& bind, bool accum) {
    bind.storageBuf(70, kSharcHashSlot, false);
    if (accum) {
        bind.storageBuf(71, kSharcAccumSlot, false);
    }
    bind.storageBuf(72, kSharcResolvedSlot, false);
    bind.ubo(73, static_cast<HeapBuf>(kSharcParamSlot));
}

void bindHit(PassBindings& bind) {
    bind.ubo(0, HeapBuf::Frame);
    bind.sun();
    bind.knob(50, HeapBuf::Flags);
    bind.sampledImg(12, HeapImg::Textures);
    bind.storageBuf(14, HeapBuf::Materials);
    bind.sampler(23, HeapSamp::Linear);
    bind.sampler(24, HeapSamp::Wrap);
    bind.storageBuf(25, HeapBuf::Lights);
    bind.storageBuf(30, HeapBuf::Rc);
    bind.sampledImg(42, HeapImg::SkyViewSample);
}

void fillZero(VkCommandBuffer cmd, const GpuBuffer& buf) {
    rhiBufferBarrier(cmd, buf.buffer, 0, buf.size,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cmd, buf.buffer, 0, buf.size, 0u);
    bufferBarrierTransferToCompute(cmd, buf.buffer, 0, buf.size);
}

} // namespace

bool sharcCreate(SharcPass& p, Device& d, DescriptorHeaps& heaps) {
    sharcDestroy(p, d);
    if (!d.caps.rayQuery || !d.caps.shaderInt64 || !d.caps.shaderBufferInt64Atomics) {
        spdlog::info("sharc: ray query or int64 atomics are off");
        return true;
    }
    const VkDeviceSize hashBytes = VkDeviceSize{kSharcCapacity} * 8u;
    const VkDeviceSize voxelBytes = VkDeviceSize{kSharcCapacity} * 16u;
    if (!createDefaultGpuBuffer(p.hash, d, hashBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.accum, d, voxelBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.resolved, d, voxelBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.params, d, kParamStride * 2u, true)) {
        spdlog::error("sharc buffers failed");
        sharcDestroy(p, d);
        return false;
    }
    if (!heapWriteStorage(heaps, d, kSharcHashSlot, p.hash.address, p.hash.size)
        || !heapWriteStorage(heaps, d, kSharcAccumSlot, p.accum.address, p.accum.size)
        || !heapWriteStorage(heaps, d, kSharcResolvedSlot, p.resolved.address, p.resolved.size)) {
        spdlog::error("sharc heap slots failed");
        sharcDestroy(p, d);
        return false;
    }
    PassBindings resolveBind{heaps};
    bindCache(resolveBind, true);
    PassBindings updateBind{heaps};
    bindHit(updateBind);
    bindCache(updateBind, true);
    PassBindings queryBind{heaps};
    queryBind.sampledImg(1, HeapImg::Vis);
    queryBind.sampledImg(2, HeapImg::Depth);
    queryBind.storageImg(3, HeapImg::HdrAStorage);
    queryBind.storageImg(4, HeapImg::Surf0);
    queryBind.storageImg(5, HeapImg::Surf1);
    bindHit(queryBind);
    bindCache(queryBind, false);
    const bool shaders = sharcShader(d, BH_SHADER_DIR "/sharc.resolve.comp.spv", resolveBind, p.resolve)
        && sharcShader(d, BH_SHADER_DIR "/sharc.update.comp.spv", updateBind, p.update)
        && sharcShader(d, BH_SHADER_DIR "/sharc.query.comp.spv", queryBind, p.query);
    if (!shaders) {
        spdlog::error("sharc shaders failed");
        sharcDestroy(p, d);
        return false;
    }
    p.live = true;
    spdlog::info("sharc: {} entries, {:.1f} MiB", kSharcCapacity, static_cast<double>(hashBytes + voxelBytes * 2u) / (1024.0 * 1024.0));
    return true;
}

void sharcDestroy(SharcPass& p, Device& d) {
    shaderDestroy(d, p.query);
    shaderDestroy(d, p.resolve);
    shaderDestroy(d, p.update);
    gpuBufferDestroy(p.params, d);
    gpuBufferDestroy(p.resolved, d);
    gpuBufferDestroy(p.accum, d);
    gpuBufferDestroy(p.hash, d);
    p.hasPrev = false;
    p.cleared = false;
    p.live = false;
    p.frameIndex = 0u;
    p.lastLog[0] = '\0';
}

void sharcRecord(
    VkCommandBuffer cmd,
    SharcPass& p,
    Device& d,
    DescriptorHeaps& heaps,
    uint32_t flight,
    VkExtent2D extent,
    float camX,
    float camY,
    float camZ,
    float giGain,
    uint32_t lightCount,
    VkDeviceAddress tlas,
    bool freeze) {
    if (cmd == VK_NULL_HANDLE || !p.live || p.update.handle == VK_NULL_HANDLE || p.resolve.handle == VK_NULL_HANDLE
        || p.query.handle == VK_NULL_HANDLE || tlas == 0 || extent.width == 0 || extent.height == 0 || deviceLost(d)) {
        if (p.live) {
            std::snprintf(p.lastLog, sizeof(p.lastLog), "sharc ждёт геометрию");
        }
        return;
    }
    if (!p.hasPrev || freeze) {
        p.prevX = camX;
        p.prevY = camY;
        p.prevZ = camZ;
        p.hasPrev = true;
    }
    SharcGpuParams params{};
    params.camX = camX;
    params.camY = camY;
    params.camZ = camZ;
    params.sceneScale = 50.0f;
    params.prevX = p.prevX;
    params.prevY = p.prevY;
    params.prevZ = p.prevZ;
    params.levelBias = 0.0f;
    params.capacity = kSharcCapacity;
    params.accumulationFrameNum = 20u;
    params.staleFrameNum = 60u;
    params.frameIndex = p.frameIndex;
    params.radianceScale = 1000.0f;
    params.roughnessThreshold = 0.4f;
    params.downscale = 5u;
    params.bouncesMax = 4u;
    params.tlasLo = static_cast<uint32_t>(tlas);
    params.tlasHi = static_cast<uint32_t>(tlas >> 32);
    params.giGain = giGain > 0.0f ? giGain : 1.0f;
    params.lightCount = lightCount < kLightCap ? lightCount : kLightCap;
    const uint32_t slot = flight & 1u;
    std::memcpy(static_cast<uint8_t*>(p.params.mapped) + kParamStride * slot, &params, sizeof(params));
    gpuBufferFlush(p.params, d, kParamStride * slot, sizeof(params));
    if (!heapWriteBufferFlight(heaps, d, slot, kSharcParamSlot, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            p.params.address + kParamStride * slot, sizeof(params))) {
        std::snprintf(p.lastLog, sizeof(p.lastLog), "sharc params");
        return;
    }
    bufferBarrierHostToShader(cmd, p.params.buffer, p.params.size);
    if (!p.cleared) {
        fillZero(cmd, p.hash);
        fillZero(cmd, p.accum);
        fillZero(cmd, p.resolved);
        p.cleared = true;
    }
    const uint32_t down = params.downscale;
    const uint32_t coarseX = (extent.width + down - 1u) / down;
    const uint32_t coarseY = (extent.height + down - 1u) / down;
    cmdBindCompute(cmd, p.update.handle);
    dispatch2D(cmd, VkExtent2D{coarseX, coarseY}, 8u);
    bufferBarrierComputeWriteToReadWrite(cmd, p.hash.buffer, p.hash.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.accum.buffer, p.accum.size);
    cmdBindCompute(cmd, p.resolve.handle);
    dispatch1D(cmd, kSharcCapacity, 256u);
    bufferBarrierComputeWriteToReadWrite(cmd, p.hash.buffer, p.hash.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.resolved.buffer, p.resolved.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.accum.buffer, p.accum.size);
    cmdBindCompute(cmd, p.query.handle);
    dispatch2D(cmd, extent, 8u);
    if (!freeze) {
        p.prevX = camX;
        p.prevY = camY;
        p.prevZ = camZ;
    }
    p.frameIndex += 1u;
    std::snprintf(p.lastLog, sizeof(p.lastLog), "sharc %u  %ux%u / %u", kSharcCapacity, extent.width, extent.height, down);
}

} // namespace burnhope

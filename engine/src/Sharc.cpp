#include "render/Sharc.hpp"

#include "gfx/Light.hpp"
#include "render/HeapBind.hpp"
#include "render/PassAllocators.hpp"
#include "render/PassBufferSync.hpp"
#include "render/PassCommon.hpp"
#include "render/PassTargets.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr VkDeviceSize kParamStride = 256;

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
    bind.sampler(24, HeapSamp::Wrap);
    bind.storageBuf(25, HeapBuf::Lights);
    bind.storageBuf(30, HeapBuf::Rc);
}

bool sharcScreen(SharcPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    gpuBufferDestroy(p.color, d);
    p.screenCleared = false;
    const VkDeviceSize pixels = static_cast<VkDeviceSize>(extent.width) * static_cast<VkDeviceSize>(extent.height);
    const VkDeviceSize bytes = pixels * 16u * 3u;
    if (bytes == 0u || !createDefaultGpuBuffer(p.color, d, bytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)) {
        return false;
    }
    return heapWriteStorage(heaps, d, kSharcColorSlot, p.color.address, p.color.size);
}

bool sharcImage(TrackedImage& img, Device& d, VkExtent2D extent) {
    return recreateTracked(img, d, extent, VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
}

bool bindSharcImages(SharcPass& p, Device& d, DescriptorHeaps& heaps) {
    return heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::SharcRaw), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            p.raw.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::SharcRawSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            p.raw.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::SharcHist), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            p.history.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::SharcHistSample), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            p.history.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT)
        && heapWriteImage(heaps, d, static_cast<uint32_t>(HeapImg::SharcTemporal), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            p.temporal.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_ASPECT_COLOR_BIT);
}

void fillZero(VkCommandBuffer cmd, const GpuBuffer& buf) {
    rhiBufferBarrier(cmd, buf.buffer, 0, buf.size,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
    vkCmdFillBuffer(cmd, buf.buffer, 0, buf.size, 0u);
    bufferBarrierTransferToCompute(cmd, buf.buffer, 0, buf.size);
}

} // namespace

bool sharcCreate(SharcPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    sharcDestroy(p, d);
    if (!d.caps.rayQuery || !d.caps.shaderInt64 || !d.caps.shaderBufferInt64Atomics) {
        spdlog::info("sharc: ray query or int64 atomics are off");
        return true;
    }
    const VkDeviceSize hashBytes = VkDeviceSize{kSharcCapacity} * 8u;
    const VkDeviceSize accumBytes = VkDeviceSize{kSharcCapacity} * 16u;
    const VkDeviceSize resolvedBytes = VkDeviceSize{kSharcCapacity} * 32u;
    if (!createDefaultGpuBuffer(p.hash, d, hashBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.accum, d, accumBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.resolved, d, resolvedBytes, false, VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        || !createDefaultGpuBuffer(p.params, d, kParamStride * 2u, true)
        || !sharcScreen(p, d, heaps, extent)
        || !sharcImage(p.raw, d, extent)
        || !sharcImage(p.history, d, extent)
        || !sharcImage(p.temporal, d, extent)) {
        spdlog::error("sharc buffers failed");
        sharcDestroy(p, d);
        return false;
    }
    if (!heapWriteStorage(heaps, d, kSharcHashSlot, p.hash.address, p.hash.size)
        || !heapWriteStorage(heaps, d, kSharcAccumSlot, p.accum.address, p.accum.size)
        || !heapWriteStorage(heaps, d, kSharcResolvedSlot, p.resolved.address, p.resolved.size)
        || !bindSharcImages(p, d, heaps)) {
        spdlog::error("sharc heap slots failed");
        sharcDestroy(p, d);
        return false;
    }
    PassBindings resolveBind{heaps};
    bindCache(resolveBind, true);
    PassBindings updateBind{heaps};
    bindHit(updateBind);
    updateBind.sampledImg(1, HeapImg::Vis);
    updateBind.sampledImg(2, HeapImg::Depth);
    updateBind.storageImg(5, HeapImg::Surf1);
    updateBind.storageBuf(15, HeapBuf::RestirRis, false);
    bindCache(updateBind, true);
    PassBindings queryBind{heaps};
    bindHit(queryBind);
    queryBind.sampledImg(1, HeapImg::Vis);
    queryBind.sampledImg(2, HeapImg::Depth);
    queryBind.storageImg(3, HeapImg::HdrAStorage);
    queryBind.storageImg(4, HeapImg::Surf0);
    queryBind.storageImg(5, HeapImg::Surf1);
    queryBind.storageBuf(77, kSharcColorSlot, false);
    queryBind.storageBuf(70, kSharcHashSlot, false);
    queryBind.storageBuf(72, kSharcResolvedSlot, false);
    queryBind.ubo(73, static_cast<HeapBuf>(kSharcParamSlot));
    PassBindings temporalBind{heaps};
    temporalBind.ubo(0, HeapBuf::Frame);
    temporalBind.sampledImg(2, HeapImg::Depth);
    temporalBind.storageImg(5, HeapImg::Surf1);
    temporalBind.sampler(23, HeapSamp::Linear);
    temporalBind.ubo(73, static_cast<HeapBuf>(kSharcParamSlot));
    temporalBind.storageImg(74, HeapImg::SharcRaw);
    temporalBind.sampledImg(75, HeapImg::SharcHistSample);
    temporalBind.storageImg(76, HeapImg::SharcTemporal);
    PassBindings spatialBind{heaps};
    spatialBind.ubo(0, HeapBuf::Frame);
    spatialBind.sampledImg(1, HeapImg::Vis);
    spatialBind.sampledImg(2, HeapImg::Depth);
    spatialBind.storageImg(3, HeapImg::HdrAStorage);
    spatialBind.storageImg(4, HeapImg::Surf0);
    spatialBind.storageImg(5, HeapImg::Surf1);
    spatialBind.ubo(73, static_cast<HeapBuf>(kSharcParamSlot));
    spatialBind.storageImg(76, HeapImg::SharcTemporal);
    spatialBind.storageImg(77, HeapImg::SharcHist);
    PassBindings reblurBind{heaps};
    reblurBind.ubo(0, HeapBuf::Frame);
    reblurBind.sampledImg(1, HeapImg::Vis);
    reblurBind.sampledImg(2, HeapImg::Depth);
    reblurBind.storageImg(3, HeapImg::HdrAStorage);
    reblurBind.storageImg(4, HeapImg::Surf0);
    reblurBind.storageImg(5, HeapImg::Surf1);
    reblurBind.knob(55, HeapBuf::Shade);
    reblurBind.raw.push(0, 60, VK_SPIRV_RESOURCE_TYPE_UNIFORM_BUFFER_BIT_EXT,
        heapBufOffset(*reblurBind.raw.heaps, kGradeSlot), static_cast<uint32_t>(reblurBind.raw.heaps->bufferDescSize));
    reblurBind.storageBuf(77, kSharcColorSlot, false);
    const bool shaders = createComputeShader(d, BH_SHADER_DIR "/sharc.resolve.comp.spv", resolveBind, p.resolve)
        && createComputeShader(d, BH_SHADER_DIR "/sharc.update.comp.spv", updateBind, p.update)
        && createComputeShader(d, BH_SHADER_DIR "/sharc.query.comp.spv", queryBind, p.query, 16u)
        && createComputeShader(d, BH_SHADER_DIR "/sharc.relax.temporal.comp.spv", temporalBind, p.relaxTemporal)
        && createComputeShader(d, BH_SHADER_DIR "/sharc.relax.spatial.comp.spv", spatialBind, p.relaxSpatial, 16u)
        && createComputeShader(d, BH_SHADER_DIR "/reblur.comp.spv", reblurBind, p.reblur, 16u);
    if (!shaders) {
        spdlog::error("sharc shaders failed");
        sharcDestroy(p, d);
        return false;
    }
    p.live = true;
    spdlog::info("sharc: {} entries, {:.1f} MiB", kSharcCapacity, static_cast<double>(hashBytes + accumBytes + resolvedBytes) / (1024.0 * 1024.0));
    return true;
}

bool sharcResize(SharcPass& p, Device& d, DescriptorHeaps& heaps, VkExtent2D extent) {
    if (!p.live) {
        return true;
    }
    if (!sharcScreen(p, d, heaps, extent)
        || !sharcImage(p.raw, d, extent) || !sharcImage(p.history, d, extent)
        || !sharcImage(p.temporal, d, extent)
        || !bindSharcImages(p, d, heaps)) {
        spdlog::error("sharc resize failed");
        return false;
    }
    p.hasPrev = false;
    p.frameIndex = 0u;
    return true;
}

void sharcDestroy(SharcPass& p, Device& d) {
    shaderDestroy(d, p.reblur);
    shaderDestroy(d, p.relaxSpatial);
    shaderDestroy(d, p.relaxTemporal);
    shaderDestroy(d, p.query);
    shaderDestroy(d, p.resolve);
    shaderDestroy(d, p.update);
    gpuBufferDestroy(p.color, d);
    gpuBufferDestroy(p.params, d);
    gpuBufferDestroy(p.resolved, d);
    gpuBufferDestroy(p.accum, d);
    gpuBufferDestroy(p.hash, d);
    destroyTracked(d, {&p.raw, &p.history, &p.temporal});
    p.hasPrev = false;
    p.cleared = false;
    p.screenCleared = false;
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
    const float viewProj[16],
    float giGain,
    uint32_t lightCount,
    VkDeviceAddress tlas,
    bool freeze,
    uint32_t restirSlice,
    uint32_t restirOn,
    float shadowBias,
    float shadowRange,
    float sunDisk,
    uint32_t sunRays,
    uint32_t denoise,
    GpuProfiler& profiler) {
    if (cmd == VK_NULL_HANDLE || !p.live || p.update.handle == VK_NULL_HANDLE || p.resolve.handle == VK_NULL_HANDLE
        || p.query.handle == VK_NULL_HANDLE || p.relaxTemporal.handle == VK_NULL_HANDLE || p.relaxSpatial.handle == VK_NULL_HANDLE
        || p.reblur.handle == VK_NULL_HANDLE
        || tlas == 0 || extent.width == 0 || extent.height == 0 || deviceLost(d)) {
        if (p.live) {
            std::snprintf(p.lastLog, sizeof(p.lastLog), "sharc ждёт геометрию");
        }
        return;
    }
    if (!p.hasPrev || freeze) {
        p.prevX = camX;
        p.prevY = camY;
        p.prevZ = camZ;
        if (viewProj != nullptr) {
            std::memcpy(p.prevVp, viewProj, sizeof(p.prevVp));
        }
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
    params.accumulationFrameNum = 32u;
    params.staleFrameNum = 128u;
    params.frameIndex = p.frameIndex;
    params.radianceScale = 1000.0f; // Канонический SHARC_RADIANCE_SCALE = 1e3f (исключает переполнение uint32)
    params.roughnessThreshold = 0.4f;
    params.downscale = 8u;
    params.bouncesMax = 2u;
    params.tlasLo = static_cast<uint32_t>(tlas);
    params.tlasHi = static_cast<uint32_t>(tlas >> 32);
    params.giGain = giGain > 0.0f ? giGain : 1.0f;
    params.lightCount = lightCount < kLightCap ? lightCount : kLightCap;
    params.restirSlice = restirSlice;
    params.restirOn = restirOn;
    params.shadowBias = shadowBias > 0.0f ? shadowBias : 0.02f;
    params.shadowRange = shadowRange;
    params.sunDisk = sunDisk;
    params.sunRays = sunRays == 0u ? 1u : sunRays;
    std::memcpy(params.prevVp, p.prevVp, sizeof(params.prevVp));
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
    if (!p.screenCleared && p.color.buffer != VK_NULL_HANDLE) {
        fillZero(cmd, p.color);
        p.screenCleared = true;
    }
    if (p.history.layout == VK_IMAGE_LAYOUT_UNDEFINED || p.raw.layout == VK_IMAGE_LAYOUT_UNDEFINED
        || p.temporal.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
        const VkClearColorValue zero{};
        const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        TrackedImage* pics[] = {&p.raw, &p.history, &p.temporal};
        for (TrackedImage* pic : pics) {
            imageBarrier(cmd, *pic, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            vkCmdClearColorImage(cmd, pic->image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
            imageBarrier(cmd, *pic, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        }
    }
    const uint32_t down = params.downscale;
    const uint32_t coarseX = (extent.width + down - 1u) / down;
    const uint32_t coarseY = (extent.height + down - 1u) / down;
    {
        GpuZone zone(profiler, cmd, "GI update");
        cmdBindCompute(cmd, p.update.handle);
        dispatch2D(cmd, VkExtent2D{coarseX, coarseY}, 8u);
    }
    bufferBarrierComputeWriteToReadWrite(cmd, p.hash.buffer, p.hash.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.accum.buffer, p.accum.size);
    {
        GpuZone zone(profiler, cmd, "GI resolve");
        cmdBindCompute(cmd, p.resolve.handle);
        dispatch1D(cmd, kSharcCapacity, 256u);
    }
    bufferBarrierComputeWriteToReadWrite(cmd, p.hash.buffer, p.hash.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.resolved.buffer, p.resolved.size);
    bufferBarrierComputeWriteToReadWrite(cmd, p.accum.buffer, p.accum.size);
    {
        GpuZone zone(profiler, cmd, "GI query");
        cmdBindCompute(cmd, p.query.handle);
        cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, denoise, 0u);
        dispatch2D(cmd, extent, 8u);
    }
    if (denoise != 0u && p.color.buffer != VK_NULL_HANDLE) {
        bufferBarrierComputeWriteToReadWrite(cmd, p.color.buffer, p.color.size);
        cmdBindCompute(cmd, p.reblur.handle);
        GpuZone zone(profiler, cmd, "GI denoise");
        for (uint32_t pass = 0u; pass < 3u; ++pass) {
            cmdPushFlightMode(cmd, VK_SHADER_STAGE_COMPUTE_BIT, flight, pass);
            dispatch2D(cmd, extent, 8u);
            if (pass + 1u < 3u) {
                bufferBarrierComputeWriteToReadWrite(cmd, p.color.buffer, p.color.size);
            }
        }
    }
    if (!freeze) {
        p.prevX = camX;
        p.prevY = camY;
        p.prevZ = camZ;
        if (viewProj != nullptr) {
            std::memcpy(p.prevVp, viewProj, sizeof(p.prevVp));
        }
    }
    p.frameIndex += 1u;
    std::snprintf(p.lastLog, sizeof(p.lastLog), "sharc %u  %ux%u / %u  %s", kSharcCapacity, extent.width, extent.height, down, denoise != 0u ? "шум" : "сырой");
}

} // namespace burnhope
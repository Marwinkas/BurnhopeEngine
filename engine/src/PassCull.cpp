#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

namespace burnhope {

void framePassCull(const RenderContext& ctx, SceneFrameImpl& scene, const CullBuffers& bufs, CullResults& res) {
    GpuZone zone(ctx.profiler, ctx.cmd, "Shadow_Cull");
    shadowCullRecord(ctx.cmd, scene.shadowCull, bufs.shadowIndirect, bufs.shadowVisible, std::max(bufs.instanceCount, 1u));
    const bool occlOn = scene.occl.compact[0].handle != VK_NULL_HANDLE && bufs.indirect.mapped != nullptr;
    res.twoPhase = occlOn;
    if (!res.twoPhase) {
        return;
    }
    auto* args = static_cast<uint32_t*>(bufs.indirect.mapped) + ctx.flight * 16u;
    const uint32_t pair = ctx.flight * 2u * bufs.strideU;
    const uint32_t flagRead = scene.flagFlip[ctx.flight] ? pair + bufs.strideU : pair;
    const uint32_t flagWrite = scene.flagFlip[ctx.flight] ? pair : pair + bufs.strideU;
    scene.flagFlip[ctx.flight] = !scene.flagFlip[ctx.flight];
    scene.occlIn = bufs.instanceCount;
    res.occlOut = args[0] + args[8];
    scene.occlOut = res.occlOut;
    res.visibleCount = res.occlOut;
    scene.visibleCount = res.visibleCount;
    args[4] = bufs.instanceCount;
    args[5] = 1u;
    args[6] = bufs.strideU * ctx.flight * 2u;
    args[7] = flagRead;
    args[12] = flagWrite;
    static uint32_t cullTick = 0;
    if (++cullTick % 60u == 0u) {
        const auto* dbg = static_cast<const uint32_t*>(bufs.indirect.mapped) + 64u + ctx.flight * 16u;
        const uint32_t keep0 = bufs.sunKeep != nullptr ? bufs.sunKeep[0] : 0u;
        const uint32_t keep1 = bufs.sunKeep != nullptr ? bufs.sunKeep[1] : 0u;
        const uint32_t keep2 = bufs.sunKeep != nullptr ? bufs.sunKeep[2] : 0u;
        spdlog::info(
            "cull in {} early {} late {} | skipE {} skipL {} | lod {} behind {} frust {} cone {} hiz {} near {} subpx {} | sun {} {} {} | cpu {:.2f} | profile {} hiz {} cone {} lodc {} frc {} small {} sh {} mode {}",
            bufs.instanceCount, args[0], args[8], dbg[0], dbg[7],
            dbg[1], dbg[2], dbg[3], dbg[4], dbg[5], dbg[6], dbg[8],
            keep0, keep1, keep2, scene.cpuRecordMs,
            scene.lightProfile, scene.showHiz ? 1 : 0, scene.showCone ? 1 : 0,
            scene.showLod ? 1 : 0, scene.showFrustum ? 1 : 0, scene.showSubpixel ? 1 : 0, scene.showShadow ? 1 : 0,
            scene.debugMode);
    }
    {
        GpuZone zoneEarly(ctx.profiler, ctx.cmd, "Mesh_Cull");
        occlPassRecord(ctx.cmd, scene.occl, bufs.indirect, bufs.candidates, bufs.visible, std::max(bufs.instanceCount, 1u), false, ctx.flight);
    }
    res.draw.indirect = bufs.indirect.buffer;
    res.draw.indirectOffset = static_cast<VkDeviceSize>(ctx.flight) * 64u;
    res.draw.visibleCount = 0;
    res.lateDraw = res.draw;
    res.lateDraw.indirectOffset = res.draw.indirectOffset + 32u;
}

} // namespace burnhope

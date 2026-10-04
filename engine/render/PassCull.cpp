#include "render/SceneFrameImpl.hpp"

#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassCull(FramePass& p) {
    GpuZone zone(p.scene->gpuTime, p.cmd, "Shadow_Cull");
    shadowCullRecord(p.cmd, p.scene->shadowCull, p.scene->shadowIndirect, p.scene->shadowVisible, std::max(p.scene->instanceCount, 1u));
    p.twoPhase = p.occl && p.scene->indirect.mapped != nullptr;
    if (!p.twoPhase) {
        return;
    }
    auto* args = static_cast<uint32_t*>(p.scene->indirect.mapped) + p.flight * 16u;
    const uint32_t pair = p.flight * 2u * p.strideU;
    const uint32_t flagRead = p.scene->flagFlip[p.flight] ? pair + p.strideU : pair;
    const uint32_t flagWrite = p.scene->flagFlip[p.flight] ? pair : pair + p.strideU;
    p.scene->flagFlip[p.flight] = !p.scene->flagFlip[p.flight];
    p.scene->occlIn = p.scene->instanceCount;
    p.scene->occlOut = args[0] + args[8];
    p.scene->visibleCount = p.scene->occlOut;
    args[4] = p.scene->instanceCount;
    args[5] = 1u;
    args[6] = p.strideU * p.flight * 2u;
    args[7] = flagRead;
    args[12] = flagWrite;
    static uint32_t cullTick = 0;
    if (++cullTick % 60u == 0u) {
        const auto* dbg = static_cast<const uint32_t*>(p.scene->indirect.mapped) + 64u + p.flight * 16u;
        spdlog::info(
            "cull in {} early {} late {} | skipE {} skipL {} | lod {} behind {} frust {} cone {} hiz {} near {} subpx {} | sun {} {} {} | cpu {:.2f} | ssr {} ssrc {} hiz {} cone {} lodc {} frc {} small {} sh {} mode {}",
            p.scene->instanceCount, args[0], args[8], dbg[0], dbg[7],
            dbg[1], dbg[2], dbg[3], dbg[4], dbg[5], dbg[6], dbg[8],
            p.sunKeep[0], p.sunKeep[1], p.sunKeep[2], p.scene->cpuRecordMs,
            p.scene->showSsr ? 1 : 0, p.scene->showSsrc ? 1 : 0, p.scene->showHiz ? 1 : 0, p.scene->showCone ? 1 : 0,
            p.scene->showLod ? 1 : 0, p.scene->showFrustum ? 1 : 0, p.scene->showSubpixel ? 1 : 0, p.scene->showShadow ? 1 : 0,
            p.scene->debugMode);
    }
    {
        GpuZone zoneEarly(p.scene->gpuTime, p.cmd, "Mesh_Cull");
        occlPassRecord(p.cmd, p.scene->occl, p.scene->indirect, p.scene->candidates, p.scene->visible, std::max(p.scene->instanceCount, 1u), false, p.flight);
    }
    p.draw.indirect = p.scene->indirect.buffer;
    p.draw.indirectOffset = static_cast<VkDeviceSize>(p.flight) * 64u;
    p.draw.visibleCount = 0;
    p.lateDraw = p.draw;
    p.lateDraw.indirectOffset = p.draw.indirectOffset + 32u;
}

} // namespace burnhope

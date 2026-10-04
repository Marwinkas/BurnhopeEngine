#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassVis(
    const RenderContext& ctx,
    VisPass& vis,
    OcclPass& occl,
    HizPass& hiz,
    TrackedImage& hizMax,
    const GpuBuffer& indirect,
    const GpuBuffer& candidates,
    const GpuBuffer& visible,
    uint32_t instanceCount,
    bool twoPhase,
    bool showHiz,
    const VisInstanceDraw& draw,
    const VisInstanceDraw& lateDraw) {
    auto release = [&]() {
        imageBarrier(ctx.cmd, vis.targets.vis, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
        imageBarrier(ctx.cmd, vis.targets.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    };
    if (twoPhase) {
        {
            GpuZone zone(ctx.profiler, ctx.cmd, "Visbuffer");
            visPassRecord(ctx.cmd, ctx.extent, vis, draw, false, ctx.flight);
        }
        release();
        if (showHiz) {
            GpuZone zone(ctx.profiler, ctx.cmd, "HiZ_Max");
            hizMaxBuildRecord(ctx.cmd, ctx.extent, hiz, hizMax, ctx.flight);
        }
        {
            GpuZone zone(ctx.profiler, ctx.cmd, "Mesh_Cull_Late");
            occlPassRecord(ctx.cmd, occl, indirect, candidates, visible, std::max(instanceCount, 1u), true, ctx.flight);
        }
        {
            GpuZone zone(ctx.profiler, ctx.cmd, "Visbuffer_Late");
            visPassRecord(ctx.cmd, ctx.extent, vis, lateDraw, true, ctx.flight);
        }
    } else {
        GpuZone zone(ctx.profiler, ctx.cmd, "Visbuffer");
        visPassRecord(ctx.cmd, ctx.extent, vis, draw, false, ctx.flight);
    }
}

} // namespace burnhope

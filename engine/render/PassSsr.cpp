#include "render/PassTargets.hpp"
#include "render/PassTransitions.hpp"
#include "render/SceneFrameImpl.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassSsr(const PassContext& ctx, SsrPass& ssr, GpuImage& hdrA, GpuImage& hdrB, TrackedImage& hiz, bool ssrOn) {
    transitionHdrStorageToSampled(ctx.cmd, hdrA.image, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    transitionHdrToStorageWrite(ctx.cmd, hdrB.image);
    if (ssrOn) {
        {
            GpuZone zone(*ctx.profiler, ctx.cmd, "HiZ_Min");
            hizBuildRecord(ctx.cmd, ctx.extent, ssr, hiz, ctx.flight);
        }
        {
            GpuZone zone(*ctx.profiler, ctx.cmd, "SSR_Trace");
            ssrTraceRecord(ctx.cmd, ctx.extent, ssr, ctx.flight);
        }
    }
    {
        GpuZone zone(*ctx.profiler, ctx.cmd, "SSR_Up");
        ssrUpRecord(ctx.cmd, ctx.extent, ssr, ctx.flight);
    }
}

} // namespace burnhope

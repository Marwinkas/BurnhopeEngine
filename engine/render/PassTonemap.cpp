#include "render/PassTargets.hpp"
#include "render/PassTransitions.hpp"
#include "render/SceneFrameImpl.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassTonemap(const PassContext& ctx, TonemapPass& tone, GpuImage& hdrA, GpuImage& hdrB, const Swapchain& swap, const FrameContext& frame, bool bloomOn) {
    transitionHdrStorageToSampled(ctx.cmd, hdrB.image);
    transitionHdrSampledToStorageWrite(ctx.cmd, hdrA.image);
    {
        GpuZone zone(*ctx.profiler, ctx.cmd, "CMAA");
        cmaaPassRecord(ctx.cmd, ctx.extent, tone);
    }
    transitionHdrStorageToSampled(ctx.cmd, hdrA.image, 0, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    {
        GpuZone zone(*ctx.profiler, ctx.cmd, "Bloom");
        bloomPassRecord(ctx.cmd, swap, tone, bloomOn);
    }
    {
        GpuZone zone(*ctx.profiler, ctx.cmd, "Tonemap");
        tonemapDrawRecord(ctx.cmd, swap, frame, tone);
    }
}

} // namespace burnhope

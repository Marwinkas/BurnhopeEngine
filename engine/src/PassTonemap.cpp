#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassTonemap(const RenderContext& ctx, TonemapPass& tone, TrackedImage& hdrA, TrackedImage& hdrB, const Swapchain& swap, const FrameContext& frame, bool bloomOn) {
    imageBarrier(ctx.cmd, hdrB, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "CMAA");
        cmaaPassRecord(ctx.cmd, ctx.extent, tone);
    }
    if ((ctx.post.flags.passMask & 4u) != 0u) {
        imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        imageBarrier(ctx.cmd, hdrB, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        GpuZone zone(ctx.profiler, ctx.cmd, "SMAA");
        smaaPassRecord(ctx.cmd, ctx.extent, tone);
    }
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Bloom");
        bloomPassRecord(ctx.cmd, swap, tone, bloomOn);
    }
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Tonemap");
        tonemapDrawRecord(ctx.cmd, swap, frame, tone);
    }
}

} // namespace burnhope

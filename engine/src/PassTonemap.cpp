#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassTonemap(const RenderContext& ctx, TonemapPass& tone, TrackedImage& hdrA, TrackedImage& hdrB, const Swapchain& swap, const FrameContext& frame, bool bloomOn, bool skipAa, bool taaOn) {
    imageBarrier(ctx.cmd, hdrB, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    if (!skipAa) {
        GpuZone zone(ctx.profiler, ctx.cmd, "CMAA");
        cmaaPassRecord(ctx.cmd, ctx.extent, tone);
    }
    if (!skipAa && (ctx.post.flags.passMask & 4u) != 0u) {
        imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        imageBarrier(ctx.cmd, hdrB, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        GpuZone zone(ctx.profiler, ctx.cmd, "SMAA");
        smaaPassRecord(ctx.cmd, ctx.extent, tone);
    }
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    const uint32_t curr = ctx.flight & 1u;
    const bool taaRun = taaOn && tone.taa.handle != VK_NULL_HANDLE && tone.history[curr].image.image != VK_NULL_HANDLE;
    if (taaRun) {
        imageBarrier(ctx.cmd, tone.history[curr], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        imageBarrier(ctx.cmd, tone.history[curr ^ 1u], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        {
            GpuZone zone(ctx.profiler, ctx.cmd, "TAA");
            dispatchCompute2D(ctx.cmd, tone.taa, ctx.extent);
        }
        imageBarrier(ctx.cmd, tone.history[curr], VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT, VK_IMAGE_ASPECT_COLOR_BIT, true);
        imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        dispatchCompute2D(ctx.cmd, tone.taaCopy, ctx.extent);
        tone.taaReady = 1u;
    } else {
        tone.taaReady = 0u;
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

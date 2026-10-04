#include "render/SceneFrameImpl.hpp"

#include "render/Sky.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassShade(const RenderContext& ctx, ShadePass& shade, SkyPass& sky, TrackedImage& vis, TrackedImage& depth, bool gtaoOn, VkBuffer probes, VkDeviceSize probeBytes) {
    imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    imageBarrier(ctx.cmd, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "GTAO");
        if (gtaoOn) {
            shadeAoRecord(ctx.cmd, ctx.extent, shade);
        } else {
            imageBarrier(ctx.cmd, shade.ao, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
    }
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Sky");
        skyLutRecord(ctx.cmd, sky);
        skyProbeRecord(ctx.cmd, sky, probes, probeBytes);
        shadeSkyRecord(ctx.cmd, ctx.extent, shade);
    }
    const VkAccessFlags2 hdrUse = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    imageBarrier(ctx.cmd, vis, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    imageBarrier(ctx.cmd, depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Contact");
        if ((ctx.post.flags.passMask & 16384u) != 0u) {
            contactRecord(ctx.cmd, ctx.extent, shade);
        }
    }
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Sun");
        shadeSunRecord(ctx.cmd, ctx.extent, shade);
    }
    imageBarrier(ctx.cmd, shade.surf0, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(ctx.cmd, shade.surf1, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
    imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Lamps");
        shadePunctualRecord(ctx.cmd, ctx.extent, shade);
    }
    if (ctx.post.flags.lightProfile >= 1u && (ctx.post.flags.passMask & 8192u) != 0u) {
        imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
        GpuZone zone(ctx.profiler, ctx.cmd, "Reflect");
        reflectRecord(ctx.cmd, ctx.extent, shade, ctx.post.flags.lightProfile);
    }
    if ((ctx.post.flags.passMask & 4096u) != 0u && ctx.post.shade.fogDensity > 0.0008f) {
        imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
        GpuZone zone(ctx.profiler, ctx.cmd, "Fog");
        shadeFogRecord(ctx.cmd, ctx.extent, shade);
    }
    {
        GpuZone zone(ctx.profiler, ctx.cmd, "Clouds");
        if ((ctx.post.flags.passMask & 2048u) != 0u) {
            imageBarrier(ctx.cmd, shade.hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, hdrUse, VK_IMAGE_ASPECT_COLOR_BIT, true);
            cloudRecord(ctx.cmd, sky, ctx.extent);
        }
    }
}

} // namespace burnhope

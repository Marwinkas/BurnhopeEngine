#include "render/SceneFrameImpl.hpp"

#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassRadiance(const RenderContext& ctx, RadiancePass& rad, TrackedImage& hdrA, bool worldRc, float rays, float spacing, float intensity, float maxDist, float sunScale, float bounce) {
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    if (worldRc && rad.geometry) {
        rhiMemoryBarrier(ctx.cmd,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
                | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
        GpuZone zone(ctx.profiler, ctx.cmd, "Radiance");
        radianceTraceArm(
            rad, ctx.device, ctx.heaps, ctx.extent, ctx.flight, rays, spacing, intensity, maxDist, sunScale, bounce,
            ctx.sun.sunDirIntensity[0], ctx.sun.sunDirIntensity[1], ctx.sun.sunDirIntensity[2], ctx.cmd);
        imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
        radianceRecord(ctx.cmd, ctx.extent, rad, ctx.freeze);
    }
}

} // namespace burnhope

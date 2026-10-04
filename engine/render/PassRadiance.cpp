#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassRadiance(const RenderContext& ctx, RadiancePass& rad, TrackedImage& hdrA, bool worldRc) {
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    if (worldRc) {
        GpuZone zone(ctx.profiler, ctx.cmd, "Radiance");
        radianceRecord(ctx.cmd, ctx.extent, rad, ctx.freeze);
    }
}

} // namespace burnhope

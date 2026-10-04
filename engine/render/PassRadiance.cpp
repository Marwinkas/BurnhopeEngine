#include "render/PassTargets.hpp"
#include "render/PassTransitions.hpp"
#include "render/SceneFrameImpl.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassRadiance(const PassContext& ctx, RadiancePass& rad, SsrcPass& ssrc, GpuImage& hdrA, bool worldRc, bool showSsrc, uint32_t debugMode) {
    transitionHdrStorageToSampled(ctx.cmd, hdrA.image,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    if (worldRc) {
        GpuZone zone(*ctx.profiler, ctx.cmd, "Radiance");
        radianceRecord(ctx.cmd, ctx.extent, rad, ctx.freeze);
    }
    if ((!worldRc || !rad.hashLive) && (showSsrc || debugMode == 4u)) {
        GpuZone zone(*ctx.profiler, ctx.cmd, "SSRC");
        ssrcPassRecord(ctx.cmd, ctx.extent, ssrc);
    }
}

} // namespace burnhope

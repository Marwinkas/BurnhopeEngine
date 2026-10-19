#include "render/SceneFrameImpl.hpp"

#include "render/Sharc.hpp"
#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassSharc(
    const RenderContext& ctx,
    SharcPass& sharc,
    TrackedImage& hdrA,
    float giGain,
    uint32_t lightCount,
    VkDeviceAddress tlas) {
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    if (tlas == 0 || !sharc.live) {
        return;
    }
    rhiMemoryBarrier(ctx.cmd,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT
            | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR);
    GpuZone zone(ctx.profiler, ctx.cmd, "SHaRC");
    sharcRecord(ctx.cmd, sharc, ctx.device, ctx.heaps, ctx.flight, ctx.extent,
        ctx.view.cameraPos[0], ctx.view.cameraPos[1], ctx.view.cameraPos[2], ctx.view.viewProj,
        giGain, lightCount, tlas, ctx.freeze);
}

} // namespace burnhope

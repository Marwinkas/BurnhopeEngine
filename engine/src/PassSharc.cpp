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
    VkDeviceAddress tlas,
    const RestirPass& restir,
    uint32_t restirOn,
    uint32_t denoise) {
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
    const bool sun = restirOn != 0u && restir.knobSunOn != 0u && restir.di.handle != VK_NULL_HANDLE;
    sharcRecord(ctx.cmd, sharc, ctx.device, ctx.heaps, ctx.flight, ctx.extent,
        ctx.view.cameraPos[0], ctx.view.cameraPos[1], ctx.view.cameraPos[2], ctx.view.viewProj,
        giGain, lightCount, tlas, ctx.freeze,
        sun ? restir.sunSlice[ctx.flight & 1u] : 0u, sun ? 1u : 0u,
        restir.knobShadowBias, restir.knobShadowRange, restir.knobSunDisk, restir.knobSunRays, denoise);
    imageBarrier(ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT, true);
}

} // namespace burnhope

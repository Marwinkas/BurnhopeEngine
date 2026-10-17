#include "render/SceneFrameImpl.hpp"

#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassVoxel(
    const RenderContext& ctx,
    VoxelPass& voxel,
    TrackedImage& hdrA,
    bool worldVx,
    uint32_t instances,
    bool moved) {
    if (!worldVx || voxel.trace.handle == VK_NULL_HANDLE) {
        return;
    }
    imageBarrier(
        ctx.cmd, hdrA, VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    GpuZone zone(ctx.profiler, ctx.cmd, "Voxel");
    voxelRecord(ctx.cmd, voxel, ctx.device, ctx.extent, ctx.flight, instances, moved, ctx.view, ctx.sun);
}

} // namespace burnhope

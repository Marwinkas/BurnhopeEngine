#include "render/SceneFrameImpl.hpp"

#include "render/PassBufferSync.hpp"
#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassClusters(const RenderContext& ctx, ClusterPass& clusters, GpuBuffer& cells, GpuBuffer& lightIndex) {
    if ((ctx.post.flags.passMask & 1u) == 0u) {
        return;
    }
    GpuZone zone(ctx.profiler, ctx.cmd, "Light_Cull");
    clusterPassRecord(ctx.cmd, clusters);
    bufferBarrierComputeWriteToRead(ctx.cmd, cells.buffer, 0, cells.size);
    bufferBarrierComputeWriteToRead(ctx.cmd, lightIndex.buffer, 0, lightIndex.size);
}

} // namespace burnhope

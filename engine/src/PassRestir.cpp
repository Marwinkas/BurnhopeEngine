#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

namespace burnhope {

void framePassRestir(
    const RenderContext& ctx,
    RestirPass& restir,
    const FrameView& view,
    const void* lights,
    uint32_t lightCount,
    uint32_t passMask,
    VkDeviceAddress tlas) {
    if (restir.di.handle == VK_NULL_HANDLE) {
        return;
    }
    restirRecord(ctx.cmd, restir, ctx.device, view, lights, lightCount, passMask, ctx.flight, ctx.extent, tlas, ctx.profiler);
}

} // namespace burnhope

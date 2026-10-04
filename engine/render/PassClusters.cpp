#include "render/SceneFrameImpl.hpp"

#include "render/PassBufferSync.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassClusters(FramePass& p) {
    if (p.knob == nullptr || (p.knob->flags.passMask & 1u) == 0u) {
        return;
    }
    GpuZone zone(p.scene->gpuTime, p.cmd, "Light_Cull");
    clusterPassRecord(p.cmd, p.scene->clusters);
    bufferBarrierComputeWriteToRead(p.cmd, p.scene->cellBuf.buffer, 0, p.scene->cellBuf.size);
    bufferBarrierComputeWriteToRead(p.cmd, p.scene->lightIndex.buffer, 0, p.scene->lightIndex.size);
}

} // namespace burnhope

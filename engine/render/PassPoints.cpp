#include "render/SceneFrameImpl.hpp"

#include "rhi/Barrier.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassPoints(FramePass& p) {
    const uint32_t pointNeed = p.scene->world.lightCount < 3u ? p.scene->world.lightCount : 3u;
    if (!p.scene->pointReady && p.scene->pointCursor >= pointNeed) {
        visPointRecord(p.cmd, p.scene->vis, p.scene->pointDepth, p.scene->pointCount, p.scene->pointVisible.address, p.shadowStride);
        p.scene->pointReady = true;
    }
    if (!p.scene->cubeReady) {
        cubePassRecord(p.cmd, p.scene->cube, p.scene->cubeColor, p.scene->cubeDepth, p.scene->cubeCount);
        p.scene->cubeReady = true;
    }
}

} // namespace burnhope

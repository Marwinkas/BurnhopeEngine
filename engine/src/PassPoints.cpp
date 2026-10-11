#include "render/SceneFrameImpl.hpp"

namespace burnhope {

void framePassPoints(
    const RenderContext& ctx,
    VisPass& vis,
    CubePass& cube,
    TrackedImage& pointDepth,
    TrackedImage& cubeColor,
    TrackedImage& cubeDepth,
    uint32_t lightCount,
    const uint32_t pointCount[3],
    const uint32_t cubeCount[6],
    uint32_t pointCursor,
    bool& pointReady,
    bool& cubeReady,
    uint32_t& cubeFace) {
    const uint32_t pointNeed = lightCount < 3u ? lightCount : 3u;
    if (!pointReady && pointCursor >= pointNeed) {
        visPointRecord(ctx.cmd, vis, pointDepth, pointCount);
        pointReady = true;
    }
    if (!cubeReady) {
        cubePassRecord(ctx.cmd, cube, cubeColor, cubeDepth, cubeCount, cubeFace, cubeFace == 0u);
        cubeFace = (cubeFace + 1u) % 6u;
        if (cubeFace == 0u) {
            cubeReady = true;
        }
    }
}

} // namespace burnhope

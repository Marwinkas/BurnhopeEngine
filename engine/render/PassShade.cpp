#include "render/SceneFrameImpl.hpp"

#include "render/PassTransitions.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassShade(FramePass& p) {
    const bool gtaoOn = p.knob != nullptr && (p.knob->flags.passMask & 16u) != 0u && p.scene->hud.gtao > 0.001f;
    transitionHdrToStorageWrite(p.cmd, p.scene->shade.hdrA.image);
    {
        GpuZone zone(p.scene->gpuTime, p.cmd, "GTAO");
        if (gtaoOn) {
            shadeAoRecord(p.cmd, p.swap->extent, p.scene->shade);
        } else {
            transitionHdrStorageToSampled(p.cmd, p.scene->shade.ao.image, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
        }
    }
    {
        GpuZone zone(p.scene->gpuTime, p.cmd, "Shade");
        shadeLitRecord(p.cmd, p.swap->extent, p.scene->shade);
    }
}

} // namespace burnhope

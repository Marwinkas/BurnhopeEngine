#include "render/SceneFrameImpl.hpp"

#include "render/PassTransitions.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassVis(FramePass& p) {
    if (p.twoPhase) {
        {
            GpuZone zone(p.scene->gpuTime, p.cmd, "Visbuffer");
            visPassRecord(p.cmd, p.swap->extent, p.scene->vis, p.draw, false, p.flight);
        }
        transitionAttachmentToSampled(p.cmd, p.scene->vis.targets.vis.image, VK_IMAGE_ASPECT_COLOR_BIT);
        transitionAttachmentToSampled(p.cmd, p.scene->vis.targets.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT);
        if (p.scene->showHiz) {
            GpuZone zone(p.scene->gpuTime, p.cmd, "HiZ_Max");
            hizMaxBuildRecord(p.cmd, p.swap->extent, p.scene->reflect, p.scene->hizMax[p.flight], p.flight);
        }
        {
            GpuZone zone(p.scene->gpuTime, p.cmd, "Mesh_Cull_Late");
            occlPassRecord(p.cmd, p.scene->occl, p.scene->indirect, p.scene->candidates, p.scene->visible, std::max(p.scene->instanceCount, 1u), true, p.flight);
        }
        {
            GpuZone zone(p.scene->gpuTime, p.cmd, "Visbuffer_Late");
            visPassRecord(p.cmd, p.swap->extent, p.scene->vis, p.lateDraw, true, p.flight);
        }
    } else {
        GpuZone zone(p.scene->gpuTime, p.cmd, "Visbuffer");
        visPassRecord(p.cmd, p.swap->extent, p.scene->vis, p.draw, false, p.flight);
    }
    transitionAttachmentToSampled(p.cmd, p.scene->vis.targets.vis.image, VK_IMAGE_ASPECT_COLOR_BIT);
    transitionAttachmentToSampled(p.cmd, p.scene->vis.targets.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT);
}

} // namespace burnhope

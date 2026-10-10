#include "render/SceneFrameImpl.hpp"

#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassSun(const RenderContext& ctx, SceneFrameImpl& scene, VisPass& vis, SunPassData& sun) {
    if (!scene.showShadow) {
        sun.stampValid = 0;
        return;
    }
    GpuZone zone(ctx.profiler, ctx.cmd, "CSM");
    {
        bool drewShadow = false;
        const VkDeviceSize shadowList = static_cast<VkDeviceSize>(sun.strideU) * sizeof(uint32_t);
        const VkDeviceSize flightBase = shadowList * 3u * ctx.flight;
        for (uint32_t c = 0; c < 3; ++c) {
            VisInstanceDraw sunDraw{sun.instances.address, sun.shadowVisible.address + flightBase + shadowList * c, 0};
            sunDraw.indirect = sun.shadowIndirect.buffer;
            sunDraw.indirectOffset = static_cast<VkDeviceSize>(ctx.flight * 64u + c * 16u);
            visShadowRecord(ctx.cmd, vis, sun.depth, sunDraw, c);
            drewShadow = true;
            std::memcpy(sun.stamp[c], ctx.sun.sunCascade[c], sizeof(sun.stamp[c]));
        }
        if (drewShadow) {
            imageBarrier(ctx.cmd, sun.depth, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
        }
        sun.stampValid = 1;
        sun.stampCount = scene.instanceCount;
    }
    static uint32_t shadowLog = 0;
    if (++shadowLog % 60u == 0u) {
        const float* sunC = sun.sunC != nullptr ? sun.sunC : scene.fly.eye;
        const float splits[3] = {
            sun.splits != nullptr ? sun.splits[0] : 0.0f,
            sun.splits != nullptr ? sun.splits[1] : 0.0f,
            sun.splits != nullptr ? sun.splits[2] : 0.0f,
        };
        spdlog::info(
            "shadow cache {} inst {}/{} center {:.1f} {:.1f} {:.1f} split {:.1f} {:.1f} {:.1f} eye {:.1f} {:.1f} {:.1f} mode {} sscs {:.2f} sh {} zen {:.1f} az {:.1f} sun {:.2f} {:.2f} {:.2f} | c0 {:.2f} {:.2f} {:.2f} | c1 {:.2f} {:.2f} {:.2f} | c2 {:.2f} {:.2f} {:.2f} | cpu {:.2f} vis {} late {}",
            0, sun.stampCount, scene.instanceCount,
            sunC[0], sunC[1], sunC[2], splits[0], splits[1], splits[2],
            scene.fly.eye[0], scene.fly.eye[1], scene.fly.eye[2],
            scene.debugMode, scene.hud.contact, 1,
            scene.atmosphere.sunZenith, scene.atmosphere.sunAzimuth,
            ctx.sun.sunDirIntensity[0], ctx.sun.sunDirIntensity[1], ctx.sun.sunDirIntensity[2],
            ctx.sun.sunCascade[0][12], ctx.sun.sunCascade[0][13], ctx.sun.sunCascade[0][14],
            ctx.sun.sunCascade[1][12], ctx.sun.sunCascade[1][13], ctx.sun.sunCascade[1][14],
            ctx.sun.sunCascade[2][12], ctx.sun.sunCascade[2][13], ctx.sun.sunCascade[2][14],
            scene.cpuRecordMs, scene.occlOut > 0 ? scene.visibleCount : 0u, scene.occlOut);
    }
}

} // namespace burnhope

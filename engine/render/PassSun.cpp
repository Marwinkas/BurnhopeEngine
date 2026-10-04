#include "render/SceneFrameImpl.hpp"

#include "render/PassTransitions.hpp"
#include "rhi/GpuProfiler.hpp"

#include <spdlog/spdlog.h>

#include <cstring>

namespace burnhope {

void framePassSun(FramePass& p) {
    if (!p.scene->showShadow) {
        p.scene->shadowStampValid = 0;
        return;
    }
    bool shadowSame = p.scene->shadowStampValid != 0
        && p.scene->shadowStampCount == p.scene->instanceCount
        && p.scene->shadowDepth[0].layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for (uint32_t c = 0; c < 3; ++c) {
        if (std::memcmp(p.scene->shadowStamp[c], p.gpu->sunCascade[c], sizeof(p.scene->shadowStamp[c])) != 0) {
            shadowSame = false;
        }
    }
    GpuZone zone(p.scene->gpuTime, p.cmd, "CSM");
    if (!shadowSame) {
        TrackedImage& depth = p.scene->shadowDepth[0];
        VkImageLayout shadowLayout = depth.layout;
        bool drewShadow = false;
        for (uint32_t c = 0; c < 3; ++c) {
            if (p.scene->shadowStampValid != 0
                && std::memcmp(p.scene->shadowStamp[c], p.gpu->sunCascade[c], sizeof(p.scene->shadowStamp[c])) == 0) {
                continue;
            }
            const VkDeviceSize shadowList = static_cast<VkDeviceSize>(p.strideU) * sizeof(uint32_t);
            VisInstanceDraw sunDraw{p.scene->instances.address, p.scene->shadowVisible.address + shadowList * c, 0};
            sunDraw.indirect = p.scene->shadowIndirect.buffer;
            sunDraw.indirectOffset = static_cast<VkDeviceSize>(p.flight * 64u + c * 16u);
            visShadowRecord(p.cmd, p.scene->vis, depth.image, sunDraw, shadowLayout, c);
            shadowLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
            drewShadow = true;
            std::memcpy(p.scene->shadowStamp[c], p.gpu->sunCascade[c], sizeof(p.scene->shadowStamp[c]));
        }
        if (drewShadow) {
            transitionAttachmentToSampled(p.cmd, depth.image.image, VK_IMAGE_ASPECT_DEPTH_BIT);
            depth.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        p.scene->shadowStampValid = 1;
        p.scene->shadowStampCount = p.scene->instanceCount;
    }
    static uint32_t shadowLog = 0;
    if (++shadowLog % 60u == 0u) {
        spdlog::info(
            "shadow cache {} inst {}/{} center {:.1f} {:.1f} {:.1f} split {:.1f} {:.1f} {:.1f} eye {:.1f} {:.1f} {:.1f} mode {} sscs {:.2f} sh {} | c0 {:.2f} {:.2f} {:.2f} | c1 {:.2f} {:.2f} {:.2f} | c2 {:.2f} {:.2f} {:.2f} | cpu {:.2f} early {} late {}",
            shadowSame ? 1 : 0, p.scene->shadowStampCount, p.scene->instanceCount,
            p.sunC[0], p.sunC[1], p.sunC[2], p.splits[0], p.splits[1], p.splits[2],
            p.scene->fly.eye[0], p.scene->fly.eye[1], p.scene->fly.eye[2],
            p.scene->debugMode, p.scene->hud.contact, 1,
            p.gpu->sunCascade[0][12], p.gpu->sunCascade[0][13], p.gpu->sunCascade[0][14],
            p.gpu->sunCascade[1][12], p.gpu->sunCascade[1][13], p.gpu->sunCascade[1][14],
            p.gpu->sunCascade[2][12], p.gpu->sunCascade[2][13], p.gpu->sunCascade[2][14],
            p.scene->cpuRecordMs, p.scene->occlOut > 0 ? p.scene->visibleCount : 0u, p.scene->occlOut);
    }
}

} // namespace burnhope

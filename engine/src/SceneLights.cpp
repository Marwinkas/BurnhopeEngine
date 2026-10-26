#include "color/Color.hpp"
#include "render/Weather.hpp"
#include "render/SceneFrameImpl.hpp"
#include "gpu_scene/LightMath.hpp"
#include "gpu_scene/PointShadow.hpp"
#include "gpu_scene/SunShadow.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>

namespace burnhope {

uint32_t uploadLights(SceneFrameImpl& s, uint32_t flight) {
    const LightGpuData* src = s.importedCount > 0 ? s.imported : s.world.lights;
    uint32_t count = s.importedCount > 0 ? s.importedCount : s.world.lightCount;
    if (count > kLightCap) {
        count = kLightCap;
    }
    if (s.lightBuf.mapped == nullptr) {
        return 0;
    }
    auto* dst = static_cast<uint8_t*>(s.lightBuf.mapped) + sizeof(LightGpuData) * kLightCap * flight;
    std::memcpy(dst, src, sizeof(LightGpuData) * count);
    if (s.handOn && count < kLightCap) {
        std::memcpy(dst + sizeof(LightGpuData) * count, &s.handLamp, sizeof(s.handLamp));
        count += 1u;
    }
    const GpuInstance* lamps = s.instSrc != nullptr ? s.instSrc : s.cpuInstances;
    const uint32_t authored = count;
    for (uint32_t i = 0; i < authored; ++i) {
        LightGpuData lamp{};
        std::memcpy(&lamp, dst + sizeof(LightGpuData) * i, sizeof(lamp));
        lightPromoteCandela(lamp);
        std::memcpy(dst + sizeof(LightGpuData) * i, &lamp, sizeof(lamp));
    }
    for (uint32_t f = 0; f < s.particles.flashCount && count < kLightCap; ++f) {
        const ParticleFlash& flash = s.particles.flash[f];
        if (flash.life <= 0.0f || flash.maxLife <= 1.0e-4f) {
            continue;
        }
        LightGpuData lamp{};
        lamp.pos[0] = flash.pos[0];
        lamp.pos[1] = flash.pos[1];
        lamp.pos[2] = flash.pos[2];
        lamp.radius = flash.radius;
        lamp.color[0] = 1.0f;
        lamp.color[1] = 0.62f;
        lamp.color[2] = 0.28f;
        lamp.intensity = flash.candela * (flash.life / flash.maxLife);
        lamp.type = kLightPoint;
        lamp.flags = 4u;
        lamp.shadowMapIndex = -1;
        lamp.sourceRadius = 0.45f;
        std::memcpy(dst + sizeof(LightGpuData) * count, &lamp, sizeof(lamp));
        count += 1u;
    }
    if (((s.tweaks.passMask & kPassRestir) != 0u || (s.tweaks.passMask & kPassSharc) != 0u)
        && s.materialSrc != nullptr && s.materialCount > 0u && lamps != nullptr) {
        for (uint32_t i = 0; i < s.instanceCount && count < kLightCap; ++i) {
            const GpuInstance& inst = lamps[i];
            if (inst.materialId >= s.materialCount || inst.sphere[3] < 0.02f) {
                continue;
            }
            const MaterialGpuData& mat = s.materialSrc[inst.materialId];
            if (mat.emissiveIntensity <= 0.001f) {
                continue;
            }
            bool near = false;
            for (uint32_t l = 0; l < authored; ++l) {
                LightGpuData prev{};
                std::memcpy(&prev, dst + sizeof(LightGpuData) * l, sizeof(prev));
                const float dx = prev.pos[0] - inst.sphere[0];
                const float dy = prev.pos[1] - inst.sphere[1];
                const float dz = prev.pos[2] - inst.sphere[2];
                if (dx * dx + dy * dy + dz * dz < 0.25f) {
                    near = true;
                    break;
                }
            }
            if (near) {
                continue;
            }
            const float luma = colorLuma(mat.baseColor[0], mat.baseColor[1], mat.baseColor[2]);
            LightGpuData lamp{};
            lamp.pos[0] = inst.sphere[0];
            lamp.pos[1] = inst.sphere[1];
            lamp.pos[2] = inst.sphere[2];
            lamp.radius = std::max(inst.sphere[3] * 8.0f, 4.0f);
            lamp.color[0] = luma < 0.08f ? 1.0f : mat.baseColor[0];
            lamp.color[1] = luma < 0.08f ? 1.0f : mat.baseColor[1];
            lamp.color[2] = luma < 0.08f ? 1.0f : mat.baseColor[2];
            lamp.intensity = mat.emissiveIntensity * 8.0f * kPhotometric;
            lamp.type = kLightPoint;
            lamp.flags = 2u;
            lamp.sourceRadius = std::max(inst.sphere[3] * 0.35f, 0.15f);
            lamp.shadowMapIndex = -1;
            std::memcpy(dst + sizeof(LightGpuData) * count, &lamp, sizeof(lamp));
            count += 1u;
        }
    }
    if (count < kLightCap) {
        std::memset(dst + sizeof(LightGpuData) * count, 0, sizeof(LightGpuData) * (kLightCap - count));
    }
    return count;
}

} // namespace burnhope

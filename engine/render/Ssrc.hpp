#pragma once

#include "render/PassTargets.hpp"
#include "rhi/Device.hpp"
#include "rhi/ShaderObject.hpp"

namespace burnhope {

struct DescriptorHeaps;

constexpr uint32_t kSsrcCascades = 4;
constexpr float kSsrcBaseRange = 0.2f;

struct SsrcInterval {
    float start = 0.0f;
    float end = 0.0f;
};

inline SsrcInterval ssrcInterval(uint32_t cascade, float base = kSsrcBaseRange) {
    float endScale = 1.0f;
    for (uint32_t i = 0; i < cascade; ++i) {
        endScale *= 4.0f;
    }
    SsrcInterval out{};
    out.start = cascade == 0 ? 0.0f : base * (endScale / 4.0f);
    out.end = base * endScale;
    return out;
}

struct SsrcPass {
    TrackedImage level[2][kSsrcCascades]{};
    ShaderExt trace[kSsrcCascades]{};
    ShaderExt apply{};
    uint32_t slot = 0;
};

[[nodiscard]] bool ssrcPassCreate(SsrcPass& p, Device& d, const DescriptorHeaps& heaps, VkExtent2D extent);
void ssrcPassDestroy(SsrcPass& p, Device& d);
void ssrcPassRecord(VkCommandBuffer cmd, VkExtent2D extent, SsrcPass& p);

} // namespace burnhope

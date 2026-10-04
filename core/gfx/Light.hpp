#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kLightCap = 10240;
constexpr uint32_t kLightDirectional = 0;
constexpr uint32_t kLightPoint = 1;
constexpr uint32_t kLightSpot = 2;

struct alignas(16) LightGpuData {
    float pos[3]{};
    float radius = 0.0f;
    float color[3]{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    float direction[3]{0.0f, -1.0f, 0.0f};
    float spotAngle = 0.0f;
    uint32_t type = kLightPoint;
    uint32_t flags = 0;
    int32_t shadowMapIndex = -1;
    uint32_t pad = 0;
};

static_assert(std::is_trivially_copyable_v<LightGpuData>);
static_assert(sizeof(LightGpuData) == 64);
static_assert(alignof(LightGpuData) == 16);

} // namespace burnhope

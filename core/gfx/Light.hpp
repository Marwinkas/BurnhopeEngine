#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kLightCap = 10240;
constexpr uint32_t kLightDirectional = 0;
constexpr uint32_t kLightPoint = 1;
constexpr uint32_t kLightSpot = 2;

// Полдень 100000 люкс и точечный источник в канделах делятся на одно число.
// 1 кд на 1 м даёт 1 люкс, и оба попадают в тот же диапазон кадра.
constexpr float kPhotometric = 5000.0f;

struct alignas(16) LightGpuData {
    float pos[3]{};
    float radius = 0.0f;
    float color[3]{1.0f, 1.0f, 1.0f};
    // Точечный и прожектор: кандела. Направленный сюда не пишется, солнце живёт в люксах.
    float intensity = 1.0f;
    float direction[3]{0.0f, -1.0f, 0.0f};
    float spotAngle = 0.0f;
    uint32_t type = kLightPoint;
    uint32_t flags = 0;
    int32_t shadowMapIndex = -1;
    float sourceRadius = 0.0f;
};

static_assert(std::is_trivially_copyable_v<LightGpuData>);
static_assert(sizeof(LightGpuData) == 64);
static_assert(alignof(LightGpuData) == 16);

// Старые сцены хранили безразмерную яркость около 8..24. Ниже 64 это ещё та шкала.
inline void lightPromoteCandela(LightGpuData& light) {
    if (light.type != kLightPoint && light.type != kLightSpot) {
        return;
    }
    if (light.intensity > 0.0f && light.intensity < 64.0f) {
        light.intensity *= kPhotometric;
    }
}

} // namespace burnhope

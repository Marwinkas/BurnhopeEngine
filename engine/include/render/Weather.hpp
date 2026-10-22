#pragma once

#include "gpu_scene/FrameUniforms.hpp"

#include <flecs.h>

#include <cstdint>
#include <type_traits>

namespace burnhope {

// Климат сцены. Один компонент на сущности погоды, шейдеры читают уже собранные величины.
struct alignas(16) Weather {
    float timeOfDay = 11.4f;
    float sunZenith = 35.0f;
    float sunAzimuth = 30.0f;
    float sunIntensity = 100000.0f;
    float sunColor[3]{1.0f, 0.92f, 0.76f};
    float cloudCoverage = 0.55f;
    float cloudDensity = 0.09f;
    float rainIntensity = 0.75f;
    float wetness = 0.55f;
    float windDir[3]{0.92f, 0.0f, 0.39f};
    float windSpeed = 6.0f;
    float fogDensity = 0.06f;
    float temperatureC = 12.0f;
    float goalCoverage = 0.55f;
    float goalDensity = 0.09f;
    float goalRain = 0.75f;
    float goalFog = 0.06f;
    float blend = 0.45f;
    float dayLength = 1200.0f;
    float thunder = 0.0f;
    float thunderWait = 8.0f;
    float windYaw = 0.4f;
    float sliderWet = 1.0f;
    float sliderZenith = 35.0f;
    float sliderAzimuth = 30.0f;
    float padTail = 0.0f;
    uint32_t preset = 2;
    uint32_t flags = 0;
    float goalWind = 6.0f;
    float sliderTime = 11.4f;
    float goalTemp = 12.0f;
    float padWx = 0.0f;
};

static_assert(sizeof(Weather) == 144);
static_assert(sizeof(Weather) % 16 == 0);
static_assert(std::is_trivially_copyable_v<Weather>);

void weatherSpawn(flecs::world& world, flecs::entity& out);
void weatherGoals(Weather& w, float coverage, float density, float rainAmount, float wetScale, float fog, float zenith, float azimuth, float wind, float yawDeg, float tempC, float daySec, float timeHours, int clockSun);
void weatherPreset(Weather& w, uint32_t preset);
void weatherIntegrate(Weather& w, float dt);
void weatherPublish(const Weather& w, AtmosphereParams& atm, float& rainAmount, float& rainWet, float& fogDensity);

}

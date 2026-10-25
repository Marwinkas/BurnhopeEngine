#include "render/Weather.hpp"

#include <flecs.h>

#include <cmath>

namespace burnhope {

namespace {

float clamp01(float v) {
    if (v < 0.0f) {
        return 0.0f;
    }
    if (v > 1.0f) {
        return 1.0f;
    }
    return v;
}

float approach(float from, float to, float k) {
    return from + (to - from) * k;
}

}

void weatherSpawn(flecs::world& world, flecs::entity& out) {
    Weather born{};
    out = world.entity("Weather").set<Weather>(born);
}

void weatherPreset(Weather& w, uint32_t preset) {
    w.preset = preset;
    if (preset == 0u) {
        w.goalCoverage = 0.12f;
        w.goalDensity = 0.04f;
        w.goalRain = 0.0f;
        w.goalWind = 2.0f;
    } else if (preset == 1u) {
        w.goalCoverage = 0.70f;
        w.goalDensity = 0.10f;
        w.goalRain = 0.10f;
        w.goalWind = 5.0f;
    } else if (preset == 2u) {
        w.goalCoverage = 0.82f;
        w.goalDensity = 0.12f;
        w.goalRain = 0.75f;
        w.goalWind = 8.0f;
    } else {
        w.preset = 3u;
        w.goalCoverage = 0.95f;
        w.goalDensity = 0.18f;
        w.goalRain = 1.0f;
        w.goalWind = 14.0f;
    }
}

void weatherGoals(Weather& w, float coverage, float density, float rainAmount, float wetScale, float fog, float zenith, float azimuth, float wind, float yawDeg, float tempC, float daySec, float timeHours, int clockSun) {
    w.goalCoverage = clamp01(coverage);
    w.goalDensity = density < 0.0f ? 0.0f : density;
    w.goalRain = clamp01(rainAmount * 0.25f);
    w.goalFog = fog < 0.0f ? 0.0f : fog;
    w.sliderWet = clamp01(wetScale);
    w.sliderZenith = zenith;
    w.sliderAzimuth = azimuth;
    w.goalWind = wind < 0.0f ? 0.0f : (wind > 40.0f ? 40.0f : wind);
    w.goalTemp = tempC;
    w.dayLength = daySec < 30.0f ? 30.0f : (daySec > 7200.0f ? 7200.0f : daySec);
    w.windYaw = yawDeg * 0.017453292f;
    if (w.windYaw >= 6.2831853f) {
        w.windYaw -= 6.2831853f;
    }
    if (w.windYaw < 0.0f) {
        w.windYaw += 6.2831853f;
    }
    if (std::fabs(timeHours - w.sliderTime) > 0.08f) {
        w.timeOfDay = timeHours;
        if (w.timeOfDay < 0.0f) {
            w.timeOfDay = 0.0f;
        }
        if (w.timeOfDay >= 24.0f) {
            w.timeOfDay = std::fmod(w.timeOfDay, 24.0f);
        }
    }
    w.sliderTime = w.timeOfDay;
    if (clockSun != 0) {
        w.flags |= 1u;
    } else {
        w.flags &= ~1u;
    }
}

void weatherIntegrate(Weather& w, float dt) {
    if (dt < 0.0f) {
        dt = 0.0f;
    }
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    const float day = w.dayLength > 1.0f ? w.dayLength : 1200.0f;
    w.timeOfDay += (dt / day) * 24.0f;
    if (w.timeOfDay >= 24.0f) {
        w.timeOfDay -= 24.0f;
    }
    const float k = 1.0f - std::exp(-w.blend * dt);
    w.cloudCoverage = approach(w.cloudCoverage, w.goalCoverage, k);
    w.cloudDensity = approach(w.cloudDensity, w.goalDensity, k);
    w.rainIntensity = approach(w.rainIntensity, w.goalRain, k);
    if ((w.flags & 1u) != 0u) {
        const float elev = std::sin((w.timeOfDay - 6.0f) * (3.14159265f / 12.0f));
        w.sunZenith = elev * 62.0f;
        w.sunAzimuth = w.timeOfDay * 15.0f;
        if (w.sunAzimuth >= 360.0f) {
            w.sunAzimuth -= 360.0f;
        }
    } else {
        w.sunZenith = approach(w.sunZenith, w.sliderZenith, k);
        w.sunAzimuth = approach(w.sunAzimuth, w.sliderAzimuth, k);
    }
    const float cover = clamp01(w.cloudCoverage);
    float moisture = clamp01(w.rainIntensity * (0.45f + 0.55f * cover));
    if (w.temperatureC < 0.0f) {
        moisture *= 0.25f;
    }
    if (moisture > 0.02f) {
        w.wetness += dt * moisture / 60.0f;
        if (w.wetness > 1.0f) {
            w.wetness = 1.0f;
        }
    } else {
        float heat = (w.temperatureC - 0.0f) / 30.0f;
        if (heat < 0.0f) {
            heat = 0.0f;
        }
        if (heat > 1.5f) {
            heat = 1.5f;
        }
        const float clear = 1.0f - cover;
        const float dry = (0.4f + heat * 1.2f + clear * 0.9f) / 180.0f;
        w.wetness -= dt * dry;
        if (w.wetness < 0.0f) {
            w.wetness = 0.0f;
        }
    }
    if (w.windYaw > 6.2831853f) {
        w.windYaw -= 6.2831853f;
    }
    w.windSpeed = approach(w.windSpeed, w.goalWind, k);
    w.windDir[0] = std::cos(w.windYaw);
    w.windDir[1] = 0.0f;
    w.windDir[2] = std::sin(w.windYaw);
    w.temperatureC = approach(w.temperatureC, w.goalTemp, k);
    w.thunder *= std::exp(-dt * 23.0f);
    w.thunderWait -= dt;
    if (w.rainIntensity > 0.62f && w.cloudCoverage > 0.72f && w.thunderWait <= 0.0f) {
        w.thunder = 1.0f;
        w.thunderWait = 11.0f;
    }
}

void weatherPublish(const Weather& w, AtmosphereParams& atm, float& rainAmount, float& rainWet, float& fogDensity) {
    const float cover = clamp01(w.cloudCoverage);
    const float rain = clamp01(w.rainIntensity);
    float lux = (100000.0f + (4200.0f - 100000.0f) * cover) * (1.0f + (0.72f - 1.0f) * rain);
    if (w.thunder > 0.02f) {
        lux = 500000.0f * w.thunder;
    }
    atm.sunZenith = w.sunZenith;
    atm.sunAzimuth = w.sunAzimuth;
    atm.sunIntensity = (w.flags & 2u) != 0u ? lux : 0.0f;
    float disk = w.sunAngle;
    if (disk < 0.05f) {
        disk = 0.05f;
    }
    if (disk > 8.0f) {
        disk = 8.0f;
    }
    atm.sunDisk = disk;
    atm.cloudCoverage = clamp01(w.cloudCoverage);
    atm.cloudDensity = w.cloudDensity < 0.0f ? 0.0f : w.cloudDensity;
    atm.cloudDetail = 0.22f + clamp01(w.cloudDensity) * 0.45f;
    const float drift = 3.0f + w.windSpeed;
    atm.cloudSpeedX = w.windDir[0] * drift;
    atm.cloudSpeedZ = w.windDir[2] * drift;
    atm.reserved0 = w.windDir[0] * w.windSpeed * 0.42f;
    atm.reserved1 = w.windDir[2] * w.windSpeed * 0.42f;
    rainAmount = clamp01(w.rainIntensity) * 4.0f;
    rainWet = clamp01(w.wetness) * w.sliderWet;
    float cold = (12.0f - w.temperatureC) / 20.0f;
    if (cold < 0.0f) {
        cold = 0.0f;
    }
    if (cold > 1.5f) {
        cold = 1.5f;
    }
    atm.mie = 1.85f * (1.0f + cold * 0.45f);
    atm.pad[0] = w.temperatureC;
    float fog = w.goalFog;
    if (w.rainIntensity > 0.5f) {
        fog *= 1.35f;
    } else {
        fog *= 1.0f + w.rainIntensity * 0.2f;
    }
    fog *= 1.0f + cold * (0.85f + clamp01(w.wetness));
    if (w.temperatureC > 28.0f) {
        fog *= 0.62f;
    }
    fogDensity = fog;
}

}

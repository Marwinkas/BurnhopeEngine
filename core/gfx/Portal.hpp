#pragma once

#include <cstdint>

namespace burnhope {

struct Portal {
    float center[3]{};
    float radius = 0.0f;
    uint32_t sector = 0;
};

struct Sector {
    float min[3]{};
    float max[3]{};
    uint32_t portalBegin = 0;
    uint32_t portalCount = 0;
};

inline bool portalSphereInFrustum(const float* plane, const Portal& portal) {
    if (plane == nullptr) {
        return true;
    }
    const float x = portal.center[0];
    const float y = portal.center[1];
    const float z = portal.center[2];
    const float r = portal.radius;
    for (uint32_t i = 0; i < 6; ++i) {
        const float* p = plane + i * 4;
        const float dist = p[0] * x + p[1] * y + p[2] * z + p[3];
        if (dist < -r) {
            return false;
        }
    }
    return true;
}

// A sector with no portals stays. Markup is what cuts a closed room.
inline bool sectorVisible(const Sector& sector, const Portal* portals, uint32_t portalCount, const float* plane) {
    if (sector.portalCount == 0 || portals == nullptr) {
        return true;
    }
    const uint32_t end = sector.portalBegin + sector.portalCount;
    for (uint32_t i = sector.portalBegin; i < end && i < portalCount; ++i) {
        if (portalSphereInFrustum(plane, portals[i])) {
            return true;
        }
    }
    return false;
}

// No markup means sector 0 is the whole world and every bit stays open.
inline uint32_t sectorVisibleMask(const Sector* sectors, uint32_t sectorCount, const Portal* portals, uint32_t portalCount, const float* plane) {
    if (sectors == nullptr || sectorCount == 0) {
        return 0xffffffffu;
    }
    uint32_t mask = 0;
    const uint32_t n = sectorCount < 32u ? sectorCount : 32u;
    for (uint32_t i = 0; i < n; ++i) {
        if (sectorVisible(sectors[i], portals, portalCount, plane)) {
            mask |= 1u << i;
        }
    }
    return mask;
}

} // namespace burnhope

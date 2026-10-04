#pragma once

#include "gfx/Light.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace burnhope {

constexpr uint32_t kClusterX = 16;
constexpr uint32_t kClusterY = 9;
constexpr uint32_t kClusterZ = 24;
constexpr uint32_t kClusterCount = kClusterX * kClusterY * kClusterZ;

struct ClusterCell {
    uint32_t offset = 0;
    uint32_t count = 0;
};

inline uint32_t clusterIndex(uint32_t x, uint32_t y, uint32_t z) {
    return x + y * kClusterX + z * kClusterX * kClusterY;
}

inline bool clusterSphereHitsCell(float cx, float cy, float cz, float radius, uint32_t x, uint32_t y, uint32_t z) {
    const float minX = static_cast<float>(x);
    const float minY = static_cast<float>(y);
    const float minZ = static_cast<float>(z);
    const float px = cx < minX ? minX : (cx > minX + 1.0f ? minX + 1.0f : cx);
    const float py = cy < minY ? minY : (cy > minY + 1.0f ? minY + 1.0f : cy);
    const float pz = cz < minZ ? minZ : (cz > minZ + 1.0f ? minZ + 1.0f : cz);
    const float dx = cx - px;
    const float dy = cy - py;
    const float dz = cz - pz;
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

inline bool clusterAssignLights(ClusterCell* cells, uint32_t* indices, uint32_t indexCap, uint32_t& indexCount, const LightGpuData* lights, uint32_t lightCount) {
    indexCount = 0;
    if (cells == nullptr || indices == nullptr) {
        return false;
    }
    std::memset(cells, 0, sizeof(ClusterCell) * kClusterCount);
    if (lights == nullptr || lightCount == 0) {
        return true;
    }
    uint32_t counts[kClusterCount]{};
    for (uint32_t n = 0; n < lightCount; ++n) {
        const LightGpuData& light = lights[n];
        if (light.type == kLightDirectional || light.radius <= 0.0f) {
            continue;
        }
        int x0 = static_cast<int>(std::floor(light.pos[0] - light.radius));
        int y0 = static_cast<int>(std::floor(light.pos[1] - light.radius));
        int z0 = static_cast<int>(std::floor(light.pos[2] - light.radius));
        int x1 = static_cast<int>(std::floor(light.pos[0] + light.radius));
        int y1 = static_cast<int>(std::floor(light.pos[1] + light.radius));
        int z1 = static_cast<int>(std::floor(light.pos[2] + light.radius));
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (z0 < 0) {
            z0 = 0;
        }
        if (x1 >= static_cast<int>(kClusterX)) {
            x1 = static_cast<int>(kClusterX) - 1;
        }
        if (y1 >= static_cast<int>(kClusterY)) {
            y1 = static_cast<int>(kClusterY) - 1;
        }
        if (z1 >= static_cast<int>(kClusterZ)) {
            z1 = static_cast<int>(kClusterZ) - 1;
        }
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    if (clusterSphereHitsCell(light.pos[0], light.pos[1], light.pos[2], light.radius, static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z))) {
                        counts[clusterIndex(static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z))] += 1;
                    }
                }
            }
        }
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < kClusterCount; ++i) {
        cells[i].offset = total;
        cells[i].count = counts[i];
        total += counts[i];
    }
    if (total > indexCap) {
        return false;
    }
    uint32_t cursor[kClusterCount];
    for (uint32_t i = 0; i < kClusterCount; ++i) {
        cursor[i] = cells[i].offset;
    }
    for (uint32_t n = 0; n < lightCount; ++n) {
        const LightGpuData& light = lights[n];
        if (light.type == kLightDirectional || light.radius <= 0.0f) {
            continue;
        }
        int x0 = static_cast<int>(std::floor(light.pos[0] - light.radius));
        int y0 = static_cast<int>(std::floor(light.pos[1] - light.radius));
        int z0 = static_cast<int>(std::floor(light.pos[2] - light.radius));
        int x1 = static_cast<int>(std::floor(light.pos[0] + light.radius));
        int y1 = static_cast<int>(std::floor(light.pos[1] + light.radius));
        int z1 = static_cast<int>(std::floor(light.pos[2] + light.radius));
        if (x0 < 0) {
            x0 = 0;
        }
        if (y0 < 0) {
            y0 = 0;
        }
        if (z0 < 0) {
            z0 = 0;
        }
        if (x1 >= static_cast<int>(kClusterX)) {
            x1 = static_cast<int>(kClusterX) - 1;
        }
        if (y1 >= static_cast<int>(kClusterY)) {
            y1 = static_cast<int>(kClusterY) - 1;
        }
        if (z1 >= static_cast<int>(kClusterZ)) {
            z1 = static_cast<int>(kClusterZ) - 1;
        }
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const uint32_t xw = static_cast<uint32_t>(x);
                    const uint32_t yw = static_cast<uint32_t>(y);
                    const uint32_t zw = static_cast<uint32_t>(z);
                    if (!clusterSphereHitsCell(light.pos[0], light.pos[1], light.pos[2], light.radius, xw, yw, zw)) {
                        continue;
                    }
                    const uint32_t id = clusterIndex(xw, yw, zw);
                    indices[cursor[id]] = n;
                    cursor[id] += 1;
                }
            }
        }
    }
    indexCount = total;
    return true;
}

} // namespace burnhope

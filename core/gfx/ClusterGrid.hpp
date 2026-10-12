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

struct ClusterGridAabb {
    float min[3]{0.0f, 0.0f, 0.0f};
    float max[3]{static_cast<float>(kClusterX), static_cast<float>(kClusterY), static_cast<float>(kClusterZ)};
};

inline uint32_t clusterIndex(uint32_t x, uint32_t y, uint32_t z) {
    return x + y * kClusterX + z * kClusterX * kClusterY;
}

inline bool clusterSphereHitsAabb(float cx, float cy, float cz, float radius, float minX, float minY, float minZ, float maxX, float maxY, float maxZ) {
    const float px = cx < minX ? minX : (cx > maxX ? maxX : cx);
    const float py = cy < minY ? minY : (cy > maxY ? maxY : cy);
    const float pz = cz < minZ ? minZ : (cz > maxZ ? maxZ : cz);
    const float dx = cx - px;
    const float dy = cy - py;
    const float dz = cz - pz;
    return dx * dx + dy * dy + dz * dz <= radius * radius;
}

inline bool clusterGridScale(const ClusterGridAabb& grid, float scale[3]) {
    const float spanX = grid.max[0] - grid.min[0];
    const float spanY = grid.max[1] - grid.min[1];
    const float spanZ = grid.max[2] - grid.min[2];
    if (spanX <= 1.0e-6f || spanY <= 1.0e-6f || spanZ <= 1.0e-6f) {
        return false;
    }
    scale[0] = static_cast<float>(kClusterX) / spanX;
    scale[1] = static_cast<float>(kClusterY) / spanY;
    scale[2] = static_cast<float>(kClusterZ) / spanZ;
    return true;
}

inline void clusterCellWorld(const ClusterGridAabb& grid, uint32_t x, uint32_t y, uint32_t z, float& minX, float& minY, float& minZ, float& maxX, float& maxY, float& maxZ) {
    const float cellX = (grid.max[0] - grid.min[0]) / static_cast<float>(kClusterX);
    const float cellY = (grid.max[1] - grid.min[1]) / static_cast<float>(kClusterY);
    const float cellZ = (grid.max[2] - grid.min[2]) / static_cast<float>(kClusterZ);
    minX = grid.min[0] + static_cast<float>(x) * cellX;
    minY = grid.min[1] + static_cast<float>(y) * cellY;
    minZ = grid.min[2] + static_cast<float>(z) * cellZ;
    maxX = minX + cellX;
    maxY = minY + cellY;
    maxZ = minZ + cellZ;
}

inline bool clusterLightRange(const ClusterGridAabb& grid, const float scale[3], float px, float py, float pz, float radius, int& x0, int& y0, int& z0, int& x1, int& y1, int& z1) {
    x0 = static_cast<int>(std::floor((px - radius - grid.min[0]) * scale[0]));
    y0 = static_cast<int>(std::floor((py - radius - grid.min[1]) * scale[1]));
    z0 = static_cast<int>(std::floor((pz - radius - grid.min[2]) * scale[2]));
    x1 = static_cast<int>(std::floor((px + radius - grid.min[0]) * scale[0]));
    y1 = static_cast<int>(std::floor((py + radius - grid.min[1]) * scale[1]));
    z1 = static_cast<int>(std::floor((pz + radius - grid.min[2]) * scale[2]));
    if (x1 < 0 || y1 < 0 || z1 < 0) {
        return false;
    }
    if (x0 >= static_cast<int>(kClusterX) || y0 >= static_cast<int>(kClusterY) || z0 >= static_cast<int>(kClusterZ)) {
        return false;
    }
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
    return true;
}

inline bool clusterAssignLights(ClusterCell* cells, uint32_t* indices, uint32_t indexCap, uint32_t& indexCount, const LightGpuData* lights, uint32_t lightCount, const ClusterGridAabb& grid = {}) {
    indexCount = 0;
    if (cells == nullptr || indices == nullptr) {
        return false;
    }
    std::memset(cells, 0, sizeof(ClusterCell) * kClusterCount);
    if (lights == nullptr || lightCount == 0) {
        return true;
    }
    float scale[3]{};
    if (!clusterGridScale(grid, scale)) {
        return false;
    }
    uint32_t counts[kClusterCount]{};
    for (uint32_t n = 0; n < lightCount; ++n) {
        const LightGpuData& light = lights[n];
        if (light.type == kLightDirectional || light.radius <= 0.0f) {
            continue;
        }
        int x0 = 0;
        int y0 = 0;
        int z0 = 0;
        int x1 = 0;
        int y1 = 0;
        int z1 = 0;
        if (!clusterLightRange(grid, scale, light.pos[0], light.pos[1], light.pos[2], light.radius, x0, y0, z0, x1, y1, z1)) {
            continue;
        }
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    float minX = 0.0f;
                    float minY = 0.0f;
                    float minZ = 0.0f;
                    float maxX = 0.0f;
                    float maxY = 0.0f;
                    float maxZ = 0.0f;
                    clusterCellWorld(grid, static_cast<uint32_t>(x), static_cast<uint32_t>(y), static_cast<uint32_t>(z), minX, minY, minZ, maxX, maxY, maxZ);
                    if (clusterSphereHitsAabb(light.pos[0], light.pos[1], light.pos[2], light.radius, minX, minY, minZ, maxX, maxY, maxZ)) {
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
        int x0 = 0;
        int y0 = 0;
        int z0 = 0;
        int x1 = 0;
        int y1 = 0;
        int z1 = 0;
        if (!clusterLightRange(grid, scale, light.pos[0], light.pos[1], light.pos[2], light.radius, x0, y0, z0, x1, y1, z1)) {
            continue;
        }
        for (int z = z0; z <= z1; ++z) {
            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const uint32_t xw = static_cast<uint32_t>(x);
                    const uint32_t yw = static_cast<uint32_t>(y);
                    const uint32_t zw = static_cast<uint32_t>(z);
                    float minX = 0.0f;
                    float minY = 0.0f;
                    float minZ = 0.0f;
                    float maxX = 0.0f;
                    float maxY = 0.0f;
                    float maxZ = 0.0f;
                    clusterCellWorld(grid, xw, yw, zw, minX, minY, minZ, maxX, maxY, maxZ);
                    if (!clusterSphereHitsAabb(light.pos[0], light.pos[1], light.pos[2], light.radius, minX, minY, minZ, maxX, maxY, maxZ)) {
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

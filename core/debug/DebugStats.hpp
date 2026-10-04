#pragma once

#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace burnhope {

struct GpuDebugStats {
    float frameTimeMs = 0.0f;
    float fps = 0.0f;
    uint32_t visibleInstances = 0;
    uint32_t culledInstances = 0;
    uint32_t visibleMeshlets = 0;
    uint32_t activeLights = 0;
    uint64_t arenaUsedBytes = 0;
    uint32_t reserved[8]{};
};

static_assert(std::is_trivially_copyable_v<GpuDebugStats>);
static_assert(sizeof(GpuDebugStats) == 64);

inline uint32_t debugFormatStats(const GpuDebugStats& stats, char* dst, uint32_t cap) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    const int wrote = std::snprintf(
        dst, cap, "fps %.1f  frame %.2f ms  vis %u  cull %u  meshlets %u  lights %u  arena %llu",
        static_cast<double>(stats.fps), static_cast<double>(stats.frameTimeMs), stats.visibleInstances, stats.culledInstances,
        stats.visibleMeshlets, stats.activeLights, static_cast<unsigned long long>(stats.arenaUsedBytes));
    if (wrote < 0) {
        dst[0] = '\0';
        return 0;
    }
    if (static_cast<uint32_t>(wrote) >= cap) {
        return cap - 1;
    }
    return static_cast<uint32_t>(wrote);
}

} // namespace burnhope

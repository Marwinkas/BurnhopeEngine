#pragma once

#include <cstddef>
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

// Строка кадра: fps, худший кадр (low), среднее по худшему проценту из последних 128 кадров и счётчик видимого.
// frameMs — последние count (до 128) времён кадров в мс. Копия сортируется на стеке, куча не нужна.
inline void debugFormatFrameStats(
    const float* frameMs, uint32_t count, float fps, uint32_t visible, uint32_t total, char* dst, size_t cap) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    const uint32_t n = frameMs != nullptr && count < 128u ? count : (frameMs != nullptr ? 128u : 0u);
    float sorted[128];
    float worst = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        sorted[i] = frameMs[i];
        if (sorted[i] > worst) {
            worst = sorted[i];
        }
    }
    for (uint32_t i = 1; i < n; ++i) {
        const float key = sorted[i];
        uint32_t j = i;
        while (j > 0 && sorted[j - 1] < key) {
            sorted[j] = sorted[j - 1];
            j -= 1;
        }
        sorted[j] = key;
    }
    const float low = worst > 0.0f ? 1000.0f / worst : fps;
    uint32_t slowN = n / 100u;
    if (slowN < 1u && n > 0u) {
        slowN = 1u;
    }
    float slowSum = 0.0f;
    for (uint32_t i = 0; i < slowN; ++i) {
        slowSum += sorted[i];
    }
    const float one = slowSum > 0.0f ? (1000.0f * static_cast<float>(slowN)) / slowSum : fps;
    std::snprintf(dst, cap, "fps %.0f   low %.0f   1%% %.0f   mesh %u/%u",
        static_cast<double>(fps), static_cast<double>(low), static_cast<double>(one), visible, total);
}

} // namespace burnhope

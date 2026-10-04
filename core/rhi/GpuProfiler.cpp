#include "rhi/GpuProfiler.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr uint32_t kQueriesPerFlight = kGpuZoneCap * 2u;

} // namespace

bool gpuProfilerCreate(GpuProfiler& p, Device& d) {
    gpuProfilerDestroy(p, d);
    if (d.device == VK_NULL_HANDLE || d.physical == VK_NULL_HANDLE) {
        return false;
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(d.physical, &props);
    p.periodNs = props.limits.timestampPeriod;
    p.timestamps = props.limits.timestampComputeAndGraphics != VK_FALSE;
    VkQueryPoolCreateInfo info{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = kQueriesPerFlight * 2u,
    };
    if (vkCreateQueryPool(d.device, &info, nullptr, &p.pool) != VK_SUCCESS) {
        p.pool = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

void gpuProfilerDestroy(GpuProfiler& p, Device& d) {
    if (p.pool != VK_NULL_HANDLE && d.device != VK_NULL_HANDLE) {
        vkDestroyQueryPool(d.device, p.pool, nullptr);
    }
    p.pool = VK_NULL_HANDLE;
    p.cursor = 0;
    p.count = 0;
    p.nestDepth = 0;
    p.armed[0] = p.armed[1] = false;
}

void gpuProfilerBegin(GpuProfiler& p, Device& d, VkCommandBuffer cmd, uint32_t flight) {
    flight &= 1u;
    p.flight = flight;
    p.cursor = 0;
    p.nestDepth = 0;
    if (p.pool == VK_NULL_HANDLE || d.device == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) {
        return;
    }
    const uint32_t base = flight * kQueriesPerFlight;
    if (p.armed[flight]) {
        for (uint32_t i = 0; i < p.count && i < kGpuZoneCap; ++i) {
            uint64_t ticks[2]{};
            const VkResult got = vkGetQueryPoolResults(
                d.device, p.pool, base + i * 2u, 2, sizeof(ticks), ticks, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
            if (got != VK_SUCCESS || ticks[1] < ticks[0]) {
                continue;
            }
            p.ms[i] = static_cast<float>(static_cast<double>(ticks[1] - ticks[0]) * static_cast<double>(p.periodNs) / 1.0e6);
        }
    }
    vkCmdResetQueryPool(cmd, p.pool, base, kQueriesPerFlight);
    p.armed[flight] = true;
}

void gpuProfilerOpen(GpuProfiler& p, VkCommandBuffer cmd, const char* name) {
    if (p.cursor >= kGpuZoneCap || p.nestDepth >= 8u) {
        return;
    }
    const uint32_t slot = p.cursor;
    p.nest[p.nestDepth] = slot;
    p.nestDepth += 1u;
    p.cursor += 1u;
    p.count = p.cursor;
    std::snprintf(p.name[slot], sizeof(p.name[slot]), "%s", name != nullptr ? name : "?");
    p.ms[slot] = 0.0f;
    if (p.pool != VK_NULL_HANDLE && cmd != VK_NULL_HANDLE) {
        const uint32_t query = p.flight * kQueriesPerFlight + slot * 2u;
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, p.pool, query);
    }
}

void gpuProfilerClose(GpuProfiler& p, VkCommandBuffer cmd) {
    if (p.nestDepth == 0) {
        return;
    }
    p.nestDepth -= 1u;
    const uint32_t slot = p.nest[p.nestDepth];
    if (p.pool != VK_NULL_HANDLE && cmd != VK_NULL_HANDLE && slot < kGpuZoneCap) {
        const uint32_t query = p.flight * kQueriesPerFlight + slot * 2u + 1u;
        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, p.pool, query);
    }
}

uint32_t gpuProfilerFormat(const GpuProfiler& p, char* dst, uint32_t cap) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (!p.timestamps && p.pool == VK_NULL_HANDLE) {
        std::snprintf(dst, cap, "gpu timestamps n/a");
        return static_cast<uint32_t>(std::strlen(dst));
    }
    float sum = 0.0f;
    for (uint32_t i = 0; i < p.count && i < kGpuZoneCap; ++i) {
        sum += p.ms[i];
    }
    int used = std::snprintf(dst, cap, "gpu %.2f ms", static_cast<double>(sum));
    if (used < 0) {
        dst[0] = '\0';
        return 0;
    }
    for (uint32_t i = 0; i < p.count && i < kGpuZoneCap; ++i) {
        if (static_cast<uint32_t>(used) + 8 >= cap) {
            break;
        }
        const int n = std::snprintf(
            dst + used, cap - static_cast<uint32_t>(used), "\n%-12s %6.2f", p.name[i], static_cast<double>(p.ms[i]));
        if (n < 0) {
            break;
        }
        used += n;
    }
    if (static_cast<uint32_t>(used) >= cap) {
        return cap - 1u;
    }
    return static_cast<uint32_t>(used);
}

} // namespace burnhope

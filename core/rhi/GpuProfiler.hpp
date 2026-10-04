#pragma once

#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kGpuZoneCap = 24;

struct GpuProfiler {
    VkQueryPool pool = VK_NULL_HANDLE;
    float periodNs = 1.0f;
    uint32_t cursor = 0;
    uint32_t flight = 0;
    uint32_t count = 0;
    uint32_t open = 0;
    bool timestamps = false;
    bool armed[2]{};
    float ms[kGpuZoneCap]{};
    char name[kGpuZoneCap][24]{};
};

[[nodiscard]] bool gpuProfilerCreate(GpuProfiler& p, Device& d);
void gpuProfilerDestroy(GpuProfiler& p, Device& d);
void gpuProfilerBegin(GpuProfiler& p, Device& d, VkCommandBuffer cmd, uint32_t flight);
void gpuProfilerOpen(GpuProfiler& p, VkCommandBuffer cmd, const char* name);
void gpuProfilerClose(GpuProfiler& p, VkCommandBuffer cmd);
[[nodiscard]] uint32_t gpuProfilerFormat(const GpuProfiler& p, char* dst, uint32_t cap);

struct GpuZone {
    GpuProfiler* prof = nullptr;
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    GpuZone(GpuProfiler& p, VkCommandBuffer c, const char* name)
        : prof(&p), cmd(c) {
        gpuProfilerOpen(p, c, name);
    }
    ~GpuZone() {
        if (prof != nullptr) {
            gpuProfilerClose(*prof, cmd);
        }
    }
    GpuZone(const GpuZone&) = delete;
    GpuZone& operator=(const GpuZone&) = delete;
};

} // namespace burnhope

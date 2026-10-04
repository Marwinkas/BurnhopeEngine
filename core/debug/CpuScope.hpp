#pragma once

#include <chrono>
#include <cstdint>

#include <spdlog/spdlog.h>

namespace burnhope {

struct CpuScope {
    const char* name = "";
    std::chrono::steady_clock::time_point t0{};
    float* slot = nullptr;

    CpuScope(const char* n, float* out)
        : name(n != nullptr ? n : ""), t0(std::chrono::steady_clock::now()), slot(out) {}

    ~CpuScope() {
        const float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (slot != nullptr) {
            *slot = ms;
        }
        if (ms <= 1.0f) {
            return;
        }
        static std::chrono::steady_clock::time_point last{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::seconds(2)) {
            return;
        }
        last = now;
        spdlog::warn("cpu {} {:.2f} ms", name, static_cast<double>(ms));
    }

    CpuScope(const CpuScope&) = delete;
    CpuScope& operator=(const CpuScope&) = delete;
};

} // namespace burnhope

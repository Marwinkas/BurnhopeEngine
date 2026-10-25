#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>

namespace burnhope {

[[nodiscard]] uint64_t platformCounter();
[[nodiscard]] uint64_t platformFrequency();
[[nodiscard]] float platformSeconds();

struct HostMeters {
    float cpu = 0.0f;
    float gpu = -1.0f;
    uint64_t rss = 0;
};

[[nodiscard]] bool platformExePath(char* dst, size_t cap);
[[nodiscard]] bool platformUserDir(char* dst, size_t cap);
// Previous CPU counters; the caller owns this so sampling has no hidden global state.
struct HostSampler {
    uint64_t prevIdle = 0;
    uint64_t prevTotal = 0;
};

void platformHostMeters(HostSampler& sampler, HostMeters& out);

bool pathParent(const char* path, char* dst, size_t cap);
bool platformCachePath(char* dst, size_t cap, const char* fileName);
// Walks the working directory, then parent folders of the executable, for relPath.
bool platformResolveAsset(const char* relPath, char* out, size_t cap);
bool platformLocalTime(const std::time_t* time, std::tm* out);

} // namespace burnhope

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
void platformHostMeters(HostMeters& out);

size_t strCopyCap(char* dst, size_t cap, const char* src);
bool pathParent(const char* path, char* dst, size_t cap);
bool platformCachePath(char* dst, size_t cap, const char* fileName);
bool platformLocalTime(const std::time_t* time, std::tm* out);

} // namespace burnhope

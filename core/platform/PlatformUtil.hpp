#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

[[nodiscard]] uint64_t platformCounter();
[[nodiscard]] uint64_t platformFrequency();
[[nodiscard]] float platformSeconds();

size_t strCopyCap(char* dst, size_t cap, const char* src);
bool pathParent(const char* path, char* dst, size_t cap);
bool platformCachePath(char* dst, size_t cap, const char* fileName);

} // namespace burnhope

#pragma once

#include <cstddef>

namespace burnhope {

size_t strCopyCap(char* dst, size_t cap, const char* src);
bool pathParent(const char* path, char* dst, size_t cap);
bool platformCachePath(char* dst, size_t cap, const char* fileName);

} // namespace burnhope

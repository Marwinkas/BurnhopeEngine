#include "platform/PlatformUtil.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace burnhope {

size_t strCopyCap(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
    return i;
}

bool pathParent(const char* path, char* dst, size_t cap) {
    if (path == nullptr || dst == nullptr || cap == 0) {
        return false;
    }
    strCopyCap(dst, cap, path);
    char* slash = std::strrchr(dst, '/');
    if (slash == nullptr) {
        return false;
    }
    if (slash == dst) {
        slash[1] = '\0';
        return true;
    }
    *slash = '\0';
    return dst[0] != '\0';
}

bool platformCachePath(char* dst, size_t cap, const char* fileName) {
    if (dst == nullptr || cap == 0 || fileName == nullptr || fileName[0] == '\0') {
        return false;
    }
    for (const char* p = fileName; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            return false;
        }
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        home = ".";
    }
    const int wrote = std::snprintf(dst, cap, "%s/.cache/%s", home, fileName);
    return wrote > 0 && static_cast<size_t>(wrote) < cap;
}

} // namespace burnhope

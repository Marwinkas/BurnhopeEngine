#pragma once

#include <cstring>

namespace burnhope {

inline void copyCap(char* dst, int cap, const char* src) {
    int i = 0;
    if (dst == nullptr || cap <= 0) {
        return;
    }
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

inline bool sameText(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::strcmp(a, b) == 0;
}

} // namespace burnhope

#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

namespace burnhope {

inline size_t strCopyCap(char* dst, size_t cap, const char* src) {
    size_t i = 0;
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
    return i;
}

inline bool strSame(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    return std::strcmp(a, b) == 0;
}

inline bool strEndsWith(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() && str.substr(str.size() - suffix.size()) == suffix;
}

inline void copyCap(char* dst, int cap, const char* src) {
    if (cap <= 0) {
        return;
    }
    strCopyCap(dst, static_cast<size_t>(cap), src);
}

inline bool sameText(const char* a, const char* b) {
    return strSame(a, b);
}

} // namespace burnhope

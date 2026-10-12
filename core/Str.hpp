#pragma once

#include <cstddef>
#include <cstdint>
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

template <size_t N>
inline uint8_t uiStrCopy(char (&dst)[N], const char* src) {
    return static_cast<uint8_t>(strCopyCap(dst, N, src));
}

inline void copyText(char* dst, uint8_t& len, const char* src) {
    char scratch[32]{};
    len = uiStrCopy(scratch, src);
    for (uint8_t i = 0; i <= len; ++i) {
        dst[i] = scratch[i];
    }
}

inline void uiWriteInt(char* dst, uint8_t& len, int v) {
    if (v < 0) {
        v = 0;
    }
    if (v > 999999) {
        v = 999999;
    }
    char rev[8]{};
    int n = 0;
    int x = v;
    if (x == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        len = 1;
        return;
    }
    while (x > 0 && n < 8) {
        rev[n++] = static_cast<char>('0' + (x % 10));
        x /= 10;
    }
    for (int i = 0; i < n; ++i) {
        dst[i] = rev[n - 1 - i];
    }
    dst[n] = '\0';
    len = static_cast<uint8_t>(n);
}

inline int uiHexNib(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

} // namespace burnhope

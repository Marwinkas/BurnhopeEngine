#pragma once

#include <cstdint>
#include <cstdio>

namespace burnhope {

inline void formatFileSize(uint64_t bytes, char* dst, int cap) {
    if (dst == nullptr || cap <= 0) {
        return;
    }
    if (bytes < 1024ull) {
        std::snprintf(dst, static_cast<size_t>(cap), "%llu Б", static_cast<unsigned long long>(bytes));
        return;
    }
    double v = static_cast<double>(bytes);
    const char* unit = "КиБ";
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        v /= 1024.0 * 1024.0 * 1024.0;
        unit = "ГиБ";
    } else if (bytes >= 1024ull * 1024ull) {
        v /= 1024.0 * 1024.0;
        unit = "МиБ";
    } else {
        v /= 1024.0;
    }
    char num[32]{};
    std::snprintf(num, sizeof(num), "%.1f", v);
    for (char* p = num; *p != '\0'; ++p) {
        if (*p == '.') {
            *p = ',';
        }
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s %s", num, unit);
}

inline const char* ruNoun(int n, const char* one, const char* few, const char* many) {
    const int a = n < 0 ? -n : n;
    const int tail = a % 100;
    const int digit = a % 10;
    if (tail >= 11 && tail <= 14) {
        return many;
    }
    if (digit == 1) {
        return one;
    }
    if (digit >= 2 && digit <= 4) {
        return few;
    }
    return many;
}

} // namespace burnhope

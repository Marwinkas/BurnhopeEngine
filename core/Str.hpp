#pragma once

#include <cstddef>
#include <cstdint>
#include <charconv>
#include <cstring>
#include <string_view>

namespace burnhope {

inline size_t strLen(const char* src) {
    if (src == nullptr) {
        return 0;
    }
    return std::strlen(src);
}

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

// Дописывает "\n" и строку в конец dst. Не влезло — строка обрезается, а без места под перевод строки пропускается.
inline size_t strAppendLine(char* dst, size_t cap, const char* line) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    size_t used = 0;
    while (used < cap && dst[used] != '\0') {
        ++used;
    }
    if (used + 2 >= cap) {
        return used;
    }
    dst[used++] = '\n';
    return used + strCopyCap(dst + used, cap - used, line);
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

inline bool strExtIs(const char* path, const char* ext) {
    if (path == nullptr || ext == nullptr) {
        return false;
    }
    const size_t n = std::strlen(path);
    const size_t m = std::strlen(ext);
    if (n < m) {
        return false;
    }
    for (size_t i = 0; i < m; ++i) {
        char a = path[n - m + i];
        char b = ext[i];
        if (a >= 'A' && a <= 'Z') {
            a = static_cast<char>(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = static_cast<char>(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

inline char strFoldAscii(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive (ASCII) substring test; an empty needle matches.
inline bool strContainsCi(std::string_view text, std::string_view needle) {
    if (needle.size() > text.size()) {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= text.size(); ++i) {
        size_t k = 0;
        while (k < needle.size() && strFoldAscii(text[i + k]) == strFoldAscii(needle[k])) {
            ++k;
        }
        if (k == needle.size()) {
            return true;
        }
    }
    return false;
}

inline bool strContainsCi(const char* text, const char* needle) {
    if (text == nullptr || needle == nullptr) {
        return false;
    }
    return strContainsCi(std::string_view(text), std::string_view(needle));
}

inline bool strIsImagePath(const char* path) {
    return strExtIs(path, ".png") || strExtIs(path, ".jpg") || strExtIs(path, ".jpeg") || strExtIs(path, ".gif")
        || strExtIs(path, ".bmp") || strExtIs(path, ".tga") || strExtIs(path, ".hdr") || strExtIs(path, ".psd");
}

// Last path component after '/' or '\\'; never null.
inline const char* strPathLeaf(const char* path) {
    const char* name = path != nullptr ? path : "";
    for (const char* p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

// Copies the leaf up to its first '.' into dst; returns the stem length.
inline size_t strPathStem(const char* path, char* dst, size_t cap) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    size_t n = 0;
    for (const char* p = strPathLeaf(path); *p != '\0' && *p != '.' && n + 1 < cap; ++p) {
        dst[n++] = *p;
    }
    dst[n] = '\0';
    return n;
}

// Number of lines; a trailing '\n' does not open a new line and empty text has none.
inline uint32_t strCountLines(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }
    uint32_t lines = 1;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n' && p[1] != '\0') {
            lines += 1;
        }
    }
    return lines;
}

// Copies line `lineIdx` (without its '\n') into dst, truncated to cap; empty when out of range.
inline size_t strCopyLine(const char* text, uint32_t lineIdx, char* dst, size_t cap) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (text == nullptr) {
        return 0;
    }
    for (uint32_t line = 0; line < lineIdx; ++line) {
        const char* nl = std::strchr(text, '\n');
        if (nl == nullptr) {
            return 0;
        }
        text = nl + 1;
    }
    const char* nl = std::strchr(text, '\n');
    size_t n = nl != nullptr ? static_cast<size_t>(nl - text) : std::strlen(text);
    if (n >= cap) {
        n = cap - 1;
    }
    std::memcpy(dst, text, n);
    dst[n] = '\0';
    return n;
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

// Reads one integer after optional blanks and advances `p` past it.
template <class T>
[[nodiscard]] inline bool strScanInt(const char*& p, const char* end, T& out) {
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    const auto r = std::from_chars(p, end, out);
    if (r.ec != std::errc{}) {
        return false;
    }
    p = r.ptr;
    return true;
}

// Matches a literal prefix at `p` and advances past it.
[[nodiscard]] inline bool strEat(const char*& p, const char* end, std::string_view lit) {
    if (static_cast<size_t>(end - p) < lit.size() || std::memcmp(p, lit.data(), lit.size()) != 0) {
        return false;
    }
    p += lit.size();
    return true;
}

} // namespace burnhope

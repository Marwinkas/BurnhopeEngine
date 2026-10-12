#include "files/Format.hpp"

#include "platform/PlatformUtil.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace burnhope {
namespace fs = std::filesystem;

void joinPath(char* dst, int cap, const char* dir, const char* name) {
    if (dir != nullptr && dir[0] == '/' && dir[1] == '\0') {
        std::snprintf(dst, static_cast<size_t>(cap), "/%s", name);
        return;
    }
    std::snprintf(dst, static_cast<size_t>(cap), "%s/%s", dir, name);
}

void parentOf(const char* path, char* dst, int cap) {
    if (dst == nullptr || cap <= 0) {
        return;
    }
    if (path == nullptr) {
        dst[0] = '\0';
        return;
    }
    pathParent(path, dst, static_cast<size_t>(cap));
}

void uniquePath(char* dst, int cap, const char* dir, const char* stem, const char* ext) {
    std::snprintf(dst, static_cast<size_t>(cap), "%s/%s%s", dir, stem, ext);
    std::error_code ec;
    int n = 2;
    while (fs::exists(fs::path(dst), ec) && n < 500) {
        std::snprintf(dst, static_cast<size_t>(cap), "%s/%s %d%s", dir, stem, n, ext);
        ++n;
    }
}

void splitStem(const char* name, char* stem, int stemCap, char* ext, int extCap) {
    const char* dot = std::strrchr(name, '.');
    if (dot == nullptr || dot == name) {
        strCopyCap(stem, stemCap > 0 ? static_cast<size_t>(stemCap) : 0, name);
        ext[0] = '\0';
        return;
    }
    const int n = static_cast<int>(dot - name);
    int i = 0;
    while (i < n && i + 1 < stemCap) {
        stem[i] = name[i];
        ++i;
    }
    stem[i] = '\0';
    strCopyCap(ext, extCap > 0 ? static_cast<size_t>(extCap) : 0, dot);
}

int fileKind(const char* name, int isDir) {
    if (isDir != 0) {
        return 1;
    }
    const char* dot = name != nullptr ? std::strrchr(name, '.') : nullptr;
    if (dot == nullptr) {
        return 0;
    }
    char ext[12]{};
    strCopyCap(ext, sizeof(ext), dot);
    for (char* p = ext; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    if (strSame(ext, ".png") || strSame(ext, ".jpg") || strSame(ext, ".jpeg")
        || strSame(ext, ".gif") || strSame(ext, ".bmp") || strSame(ext, ".tga")
        || strSame(ext, ".hdr") || strSame(ext, ".psd")) {
        return 2;
    }
    if (strSame(ext, ".txt") || strSame(ext, ".md") || strSame(ext, ".json")
        || strSame(ext, ".cpp") || strSame(ext, ".hpp") || strSame(ext, ".h")
        || strSame(ext, ".slang") || strSame(ext, ".cmake")) {
        return 3;
    }
    if (strSame(ext, ".wav") || strSame(ext, ".ogg") || strSame(ext, ".mp3")
        || strSame(ext, ".flac")) {
        return 4;
    }
    return 0;
}

const char* fileTypeName(int isDir, int kind) {
    if (isDir != 0) {
        return "Папка";
    }
    if (kind == 2) {
        return "Изображение";
    }
    if (kind == 4) {
        return "Аудио";
    }
    if (kind == 3 || kind == 0) {
        return "Текстовый документ";
    }
    return "Файл";
}

} // namespace burnhope

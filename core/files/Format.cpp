#include "files/Format.hpp"

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
    copyCap(dst, cap, path);
    char* slash = std::strrchr(dst, '/');
    if (slash == nullptr) {
        return;
    }
    if (slash == dst) {
        slash[1] = '\0';
        return;
    }
    *slash = '\0';
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
        copyCap(stem, stemCap, name);
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
    copyCap(ext, extCap, dot);
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
    copyCap(ext, 12, dot);
    for (char* p = ext; *p != '\0'; ++p) {
        if (*p >= 'A' && *p <= 'Z') {
            *p = static_cast<char>(*p - 'A' + 'a');
        }
    }
    if (std::strcmp(ext, ".png") == 0 || std::strcmp(ext, ".jpg") == 0 || std::strcmp(ext, ".jpeg") == 0
        || std::strcmp(ext, ".gif") == 0 || std::strcmp(ext, ".bmp") == 0 || std::strcmp(ext, ".tga") == 0
        || std::strcmp(ext, ".hdr") == 0 || std::strcmp(ext, ".psd") == 0) {
        return 2;
    }
    if (std::strcmp(ext, ".txt") == 0 || std::strcmp(ext, ".md") == 0 || std::strcmp(ext, ".json") == 0
        || std::strcmp(ext, ".cpp") == 0 || std::strcmp(ext, ".hpp") == 0 || std::strcmp(ext, ".h") == 0
        || std::strcmp(ext, ".slang") == 0 || std::strcmp(ext, ".cmake") == 0) {
        return 3;
    }
    if (std::strcmp(ext, ".wav") == 0 || std::strcmp(ext, ".ogg") == 0 || std::strcmp(ext, ".mp3") == 0
        || std::strcmp(ext, ".flac") == 0) {
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

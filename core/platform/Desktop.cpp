#include "platform/Desktop.hpp"

#include <cstdio>
#include <cstring>
#include <spawn.h>

extern char** environ;

namespace burnhope {
namespace {

void copyCap(char* dst, int cap, const char* src) {
    int i = 0;
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
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

void spawnLine(const char* file, char* const* argv) {
    pid_t pid = 0;
    posix_spawnp(&pid, file, nullptr, nullptr, argv, environ);
}

void fileUri(const char* path, char* dst, int cap) {
    int n = 0;
    const char* prefix = "file://";
    while (prefix[n] != '\0' && n + 1 < cap) {
        dst[n] = prefix[n];
        ++n;
    }
    for (int i = 0; path != nullptr && path[i] != '\0' && n + 4 < cap; ++i) {
        const unsigned char c = static_cast<unsigned char>(path[i]);
        const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
            || c == '/' || c == '-' || c == '_' || c == '.' || c == '~';
        if (plain) {
            dst[n++] = static_cast<char>(c);
        } else {
            std::snprintf(dst + n, static_cast<size_t>(cap - n), "%%%02X", c);
            n += 3;
        }
    }
    dst[n] = '\0';
}

} // namespace

bool desktopOpen(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    char storage[512]{};
    copyCap(storage, 512, path);
    char* argv[] = {const_cast<char*>("xdg-open"), storage, nullptr};
    pid_t pid = 0;
    return posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0;
}

void desktopReveal(const char* const* paths, int count) {
    if (paths == nullptr || count <= 0 || paths[0] == nullptr) {
        return;
    }
    char list[2048]{};
    int at = 0;
    const int n = count < 8 ? count : 8;
    for (int i = 0; i < n && at + 8 < static_cast<int>(sizeof(list)); ++i) {
        char uri[640]{};
        fileUri(paths[i], uri, 640);
        const int wrote = std::snprintf(list + at, sizeof(list) - static_cast<size_t>(at), "%s\"%s\"", i == 0 ? "" : ",", uri);
        if (wrote < 0) {
            break;
        }
        at += wrote;
    }
    char parent[512]{};
    parentOf(paths[0], parent, 512);
    char script[2400]{};
    std::snprintf(
        script,
        sizeof(script),
        "dbus-send --session --dest=org.freedesktop.FileManager1 --type=method_call "
        "/org/freedesktop/FileManager1 org.freedesktop.FileManager1.ShowItems array:string:%s string:\"\" "
        "|| xdg-open \"%s\"",
        list,
        parent);
    char* argv[] = {const_cast<char*>("sh"), const_cast<char*>("-c"), script, nullptr};
    spawnLine("sh", argv);
}

} // namespace burnhope

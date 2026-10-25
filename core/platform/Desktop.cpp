#include "platform/Desktop.hpp"
#include "Str.hpp"
#include "platform/PlatformUtil.hpp"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <shellapi.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace burnhope {
#if !defined(_WIN32)
namespace {

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

bool spawnWait(const char* file, char* const* argv) {
    pid_t pid = 0;
    if (posix_spawnp(&pid, file, nullptr, nullptr, argv, environ) != 0) {
        return false;
    }
    int status = 0;
    if (waitpid(pid, &status, 0) != pid) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

} // namespace
#endif

#if defined(_WIN32)

bool desktopOpen(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    const HINSTANCE res = ShellExecuteA(nullptr, "open", path, nullptr, nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<INT_PTR>(res) > 32;
}

void desktopReveal(const char* const* paths, int count) {
    if (paths == nullptr || count <= 0 || paths[0] == nullptr) {
        return;
    }
    char param[540]{};
    std::snprintf(param, sizeof(param), "/select,\"%s\"", paths[0]);
    ShellExecuteA(nullptr, "open", "explorer.exe", param, nullptr, SW_SHOWNORMAL);
}

#else

bool desktopOpen(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    char storage[512]{};
    strCopyCap(storage, sizeof(storage), path);
    char* argv[] = {const_cast<char*>("xdg-open"), storage, nullptr};
    pid_t pid = 0;
    return posix_spawnp(&pid, "xdg-open", nullptr, nullptr, argv, environ) == 0;
}

void desktopReveal(const char* const* paths, int count) {
    if (paths == nullptr || count <= 0 || paths[0] == nullptr) {
        return;
    }
    char uris[8][640]{};
    char list[2048]{};
    int at = 0;
    const int n = count < 8 ? count : 8;
    const char* head = "array:string:";
    while (*head != '\0' && at + 1 < static_cast<int>(sizeof(list))) {
        list[at++] = *head++;
    }
    list[at] = '\0';
    for (int i = 0; i < n; ++i) {
        fileUri(paths[i], uris[i], 640);
        const int wrote = std::snprintf(list + at, sizeof(list) - static_cast<size_t>(at), "%s%s", i == 0 ? "" : ",", uris[i]);
        if (wrote < 0) {
            break;
        }
        at += wrote;
    }
    char* argv[] = {
        const_cast<char*>("dbus-send"),
        const_cast<char*>("--session"),
        const_cast<char*>("--dest=org.freedesktop.FileManager1"),
        const_cast<char*>("--type=method_call"),
        const_cast<char*>("/org/freedesktop/FileManager1"),
        const_cast<char*>("org.freedesktop.FileManager1.ShowItems"),
        list,
        const_cast<char*>("string:"),
        nullptr,
    };
    if (spawnWait("dbus-send", argv)) {
        return;
    }
    char parent[512]{};
    if (!pathParent(paths[0], parent, sizeof(parent))) {
        strCopyCap(parent, sizeof(parent), paths[0]);
    }
    char* openArgv[] = {const_cast<char*>("xdg-open"), parent, nullptr};
    pid_t pid = 0;
    posix_spawnp(&pid, "xdg-open", nullptr, nullptr, openArgv, environ);
}

#endif

} // namespace burnhope

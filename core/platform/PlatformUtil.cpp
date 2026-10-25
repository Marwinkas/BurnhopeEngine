#include "platform/PlatformUtil.hpp"

#include "Str.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace burnhope {

bool pathParent(const char* path, char* dst, size_t cap) {
    if (path == nullptr || dst == nullptr || cap == 0) {
        return false;
    }
    strCopyCap(dst, cap, path);
    char* slash = std::strrchr(dst, '/');
    char* back = std::strrchr(dst, '\\');
    if (back != nullptr && (slash == nullptr || back > slash)) {
        slash = back;
    }
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
#if defined(_WIN32)
    const char* appData = std::getenv("LOCALAPPDATA");
    if (appData == nullptr || appData[0] == '\0') {
        appData = std::getenv("USERPROFILE");
    }
    if (appData == nullptr || appData[0] == '\0') {
        appData = ".";
    }
    const int wrote = std::snprintf(dst, cap, "%s/Burnhope/%s", appData, fileName);
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        home = ".";
    }
    const int wrote = std::snprintf(dst, cap, "%s/.cache/%s", home, fileName);
#endif
    return wrote > 0 && static_cast<size_t>(wrote) < cap;
}

uint64_t platformCounter() {
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

uint64_t platformFrequency() {
    return static_cast<uint64_t>(std::chrono::steady_clock::period::den) / static_cast<uint64_t>(std::chrono::steady_clock::period::num);
}

float platformSeconds() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<float>(now).count();
}

bool platformUserDir(char* dst, size_t cap) {
    if (dst == nullptr || cap == 0) {
        return false;
    }
    dst[0] = '\0';
#if defined(_WIN32)
    const DWORD n = GetEnvironmentVariableA("USERPROFILE", dst, static_cast<DWORD>(cap));
    return n > 0 && static_cast<size_t>(n) < cap;
#else
    const char* home = std::getenv("HOME");
    if (home == nullptr || home[0] == '\0') {
        return false;
    }
    const size_t n = strCopyCap(dst, cap, home);
    return n > 0 && dst[0] != '\0';
#endif
}

bool platformExePath(char* dst, size_t cap) {
    if (dst == nullptr || cap == 0) {
        return false;
    }
    dst[0] = '\0';
#if defined(_WIN32)
    const DWORD n = GetModuleFileNameA(nullptr, dst, static_cast<DWORD>(cap));
    return n > 0 && static_cast<size_t>(n) < cap;
#else
    const ssize_t n = ::readlink("/proc/self/exe", dst, cap - 1);
    if (n <= 0) {
        return false;
    }
    dst[n] = '\0';
    return true;
#endif
}

namespace {

int readText(const char* path, char* dst, int cap) {
#if defined(_WIN32)
    (void)path;
    (void)dst;
    (void)cap;
    return 0;
#else
    if (path == nullptr || dst == nullptr || cap <= 1) {
        return 0;
    }
    const int fd = ::open(path, O_RDONLY);
    if (fd < 0) {
        return 0;
    }
    const int n = static_cast<int>(::read(fd, dst, static_cast<size_t>(cap - 1)));
    ::close(fd);
    if (n <= 0) {
        return 0;
    }
    dst[n] = '\0';
    return n;
#endif
}

} // namespace

void platformHostMeters(HostSampler& sampler, HostMeters& out) {
    out = {};
    out.gpu = -1.0f;
#if defined(_WIN32)
    FILETIME idleFt{};
    FILETIME kernelFt{};
    FILETIME userFt{};
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt) != 0) {
        auto ticks = [](const FILETIME& ft) {
            ULARGE_INTEGER value{};
            value.LowPart = ft.dwLowDateTime;
            value.HighPart = ft.dwHighDateTime;
            return value.QuadPart;
        };
        const uint64_t idle = ticks(idleFt);
        const uint64_t total = ticks(kernelFt) + ticks(userFt);
        if (sampler.prevTotal != 0 && total > sampler.prevTotal && idle >= sampler.prevIdle) {
            const uint64_t busy = (total - sampler.prevTotal) > (idle - sampler.prevIdle) ? (total - sampler.prevTotal) - (idle - sampler.prevIdle) : 0;
            out.cpu = 100.0f * static_cast<float>(busy) / static_cast<float>(total - sampler.prevTotal);
        }
        sampler.prevIdle = idle;
        sampler.prevTotal = total;
    }
#else
    char buf[256]{};
    if (readText("/proc/stat", buf, static_cast<int>(sizeof(buf))) > 0) {
        uint64_t f[7]{};
        const char* cur = buf;
        const char* const end = buf + std::strlen(buf);
        bool ok = strEat(cur, end, "cpu");
        for (uint64_t& v : f) {
            ok = ok && strScanInt(cur, end, v);
        }
        if (ok) {
            const uint64_t idle = f[3];
            const uint64_t iowait = f[4];
            const uint64_t total = f[0] + f[1] + f[2] + f[3] + f[4] + f[5] + f[6];
            const uint64_t idleNow = idle + iowait;
            if (sampler.prevTotal != 0 && total > sampler.prevTotal) {
                out.cpu = 100.0f * static_cast<float>((total - sampler.prevTotal) - (idleNow - sampler.prevIdle)) / static_cast<float>(total - sampler.prevTotal);
            }
            sampler.prevIdle = idleNow;
            sampler.prevTotal = total;
        }
    }
    if (readText("/sys/class/drm/card1/device/gpu_busy_percent", buf, static_cast<int>(sizeof(buf))) > 0
        || readText("/sys/class/drm/card0/device/gpu_busy_percent", buf, static_cast<int>(sizeof(buf))) > 0) {
        const char* cur = buf;
        int busy = 0;
        if (strScanInt(cur, buf + std::strlen(buf), busy)) {
            out.gpu = static_cast<float>(busy);
        }
    }
    if (readText("/proc/self/statm", buf, static_cast<int>(sizeof(buf))) > 0) {
        const char* cur = buf;
        const char* const end = buf + std::strlen(buf);
        uint64_t pages = 0;
        uint64_t resident = 0;
        if (strScanInt(cur, end, pages) && strScanInt(cur, end, resident)) {
            out.rss = static_cast<uint64_t>(resident) * 4096ull;
        }
    }
#endif
}

bool platformResolveAsset(const char* relPath, char* out, size_t cap) {
    if (relPath == nullptr || relPath[0] == '\0' || out == nullptr || cap == 0) {
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path direct(relPath);
    if (fs::is_regular_file(direct, ec)) {
        strCopyCap(out, cap, relPath);
        return true;
    }
    const fs::path parentRel = fs::path("..") / relPath;
    if (fs::is_regular_file(parentRel, ec)) {
        const std::string text = parentRel.string();
        strCopyCap(out, cap, text.c_str());
        return true;
    }
    char dir[512]{};
    if (!platformExePath(dir, sizeof(dir))) {
        return false;
    }
    for (int up = 0; up < 6; ++up) {
        char parent[512]{};
        if (!pathParent(dir, parent, sizeof(parent))) {
            break;
        }
        strCopyCap(dir, sizeof(dir), parent);
        const fs::path face = fs::path(dir) / relPath;
        if (fs::is_regular_file(face, ec)) {
            const std::string text = face.string();
            strCopyCap(out, cap, text.c_str());
            return true;
        }
    }
    return false;
}

bool platformLocalTime(const std::time_t* time, std::tm* out) {
    if (time == nullptr || out == nullptr) {
        return false;
    }
#if defined(_WIN32)
    return localtime_s(out, time) == 0;
#else
    return localtime_r(time, out) != nullptr;
#endif
}

} // namespace burnhope

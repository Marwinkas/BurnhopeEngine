#include "platform/PlatformUtil.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

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

size_t strCopyCap(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    size_t i = 0;
    if (src != nullptr) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
    return i;
}

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

void platformHostMeters(HostMeters& out) {
    out = {};
    out.gpu = -1.0f;
#if defined(_WIN32)
    FILETIME idleFt{};
    FILETIME kernelFt{};
    FILETIME userFt{};
    static uint64_t prevIdle = 0;
    static uint64_t prevTotal = 0;
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt) != 0) {
        auto ticks = [](const FILETIME& ft) {
            ULARGE_INTEGER value{};
            value.LowPart = ft.dwLowDateTime;
            value.HighPart = ft.dwHighDateTime;
            return value.QuadPart;
        };
        const uint64_t idle = ticks(idleFt);
        const uint64_t total = ticks(kernelFt) + ticks(userFt);
        if (prevTotal != 0 && total > prevTotal && idle >= prevIdle) {
            const uint64_t busy = (total - prevTotal) > (idle - prevIdle) ? (total - prevTotal) - (idle - prevIdle) : 0;
            out.cpu = 100.0f * static_cast<float>(busy) / static_cast<float>(total - prevTotal);
        }
        prevIdle = idle;
        prevTotal = total;
    }
#else
    char buf[256]{};
    static uint64_t prevIdle = 0;
    static uint64_t prevTotal = 0;
    if (readText("/proc/stat", buf, static_cast<int>(sizeof(buf))) > 0) {
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t soft = 0;
        if (std::sscanf(buf, "cpu %lu %lu %lu %lu %lu %lu %lu", &user, &nice, &system, &idle, &iowait, &irq, &soft) == 7) {
            const uint64_t total = user + nice + system + idle + iowait + irq + soft;
            const uint64_t idleNow = idle + iowait;
            if (prevTotal != 0 && total > prevTotal) {
                out.cpu = 100.0f * static_cast<float>((total - prevTotal) - (idleNow - prevIdle)) / static_cast<float>(total - prevTotal);
            }
            prevIdle = idleNow;
            prevTotal = total;
        }
    }
    if (readText("/sys/class/drm/card1/device/gpu_busy_percent", buf, static_cast<int>(sizeof(buf))) > 0
        || readText("/sys/class/drm/card0/device/gpu_busy_percent", buf, static_cast<int>(sizeof(buf))) > 0) {
        out.gpu = static_cast<float>(std::atoi(buf));
    }
    if (readText("/proc/self/statm", buf, static_cast<int>(sizeof(buf))) > 0) {
        unsigned long pages = 0;
        unsigned long resident = 0;
        if (std::sscanf(buf, "%lu %lu", &pages, &resident) == 2) {
            out.rss = static_cast<uint64_t>(resident) * 4096ull;
        }
    }
#endif
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

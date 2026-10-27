#include "host/Recent.hpp"

#include "platform/PlatformUtil.hpp"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace burnhope {

bool recentFile(char* dst, size_t cap) {
    return platformCachePath(dst, cap, "recent_projects.txt");
}

bool recentLoad(RecentList& list, const char* file) {
    list = {};
    if (file == nullptr) {
        return false;
    }
    std::FILE* in = std::fopen(file, "rb");
    if (in == nullptr) {
        return true;
    }
    char buf[kRecentCap * 300u]{};
    const size_t got = std::fread(buf, 1, sizeof(buf) - 1u, in);
    std::fclose(in);
    const char* cursor = buf;
    const char* end = buf + got;
    while (cursor < end && list.count < kRecentCap) {
        const char* eol = static_cast<const char*>(std::memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
        const char* stop = eol != nullptr ? eol : end;
        const char* tab = static_cast<const char*>(std::memchr(cursor, '\t', static_cast<size_t>(stop - cursor)));
        if (tab != nullptr && stop > tab + 1) {
            RecentProject& entry = list.item[list.count];
            const auto parsed = std::from_chars(cursor, tab, entry.opened);
            const size_t len = static_cast<size_t>(stop - (tab + 1));
            if (parsed.ec == std::errc{} && len < sizeof(entry.path)) {
                std::memcpy(entry.path, tab + 1, len);
                entry.path[len] = '\0';
                ++list.count;
            } else {
                entry = {};
            }
        }
        cursor = stop + 1;
    }
    return true;
}

bool recentSave(const RecentList& list, const char* file) {
    if (file == nullptr) {
        return false;
    }
    std::FILE* out = std::fopen(file, "wb");
    if (out == nullptr) {
        return false;
    }
    bool ok = true;
    for (uint32_t i = 0; i < list.count; ++i) {
        char line[300]{};
        const int n = std::snprintf(line, sizeof(line), "%lld\t%s\n", static_cast<long long>(list.item[i].opened), list.item[i].path);
        ok = ok && n > 0 && std::fwrite(line, 1, static_cast<size_t>(n), out) == static_cast<size_t>(n);
    }
    return std::fclose(out) == 0 && ok;
}

void recentRemove(RecentList& list, uint32_t index) {
    if (index >= list.count) {
        return;
    }
    for (uint32_t i = index; i + 1u < list.count; ++i) {
        list.item[i] = list.item[i + 1u];
    }
    list.item[list.count - 1u] = {};
    --list.count;
}

void recentTouch(RecentList& list, const char* path, int64_t now) {
    if (path == nullptr || path[0] == '\0' || std::strlen(path) >= sizeof(list.item[0].path)) {
        return;
    }
    for (uint32_t i = 0; i < list.count; ++i) {
        if (std::strcmp(list.item[i].path, path) == 0) {
            recentRemove(list, i);
            break;
        }
    }
    if (list.count == kRecentCap) {
        recentRemove(list, kRecentCap - 1u);
    }
    for (uint32_t i = list.count; i > 0; --i) {
        list.item[i] = list.item[i - 1u];
    }
    list.item[0] = {};
    std::snprintf(list.item[0].path, sizeof(list.item[0].path), "%s", path);
    list.item[0].opened = now;
    ++list.count;
}

} // namespace burnhope

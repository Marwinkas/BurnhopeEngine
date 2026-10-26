#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kRecentCap = 8;

struct RecentProject {
    char path[260]{};
    int64_t opened = 0;
};

// Most recently opened first; no duplicate paths.
struct RecentList {
    RecentProject item[kRecentCap]{};
    uint32_t count = 0;
};

// One "unix-seconds<TAB>path" line per project. A missing file is an empty list.
bool recentLoad(RecentList& list, const char* file);
bool recentSave(const RecentList& list, const char* file);
// The per-user file the launcher keeps (recent_projects.txt in the platform cache folder).
bool recentFile(char* dst, size_t cap);
// Moves `path` to the front stamped `now`, dropping the oldest entry when the list is full.
void recentTouch(RecentList& list, const char* path, int64_t now);
void recentRemove(RecentList& list, uint32_t index);

} // namespace burnhope

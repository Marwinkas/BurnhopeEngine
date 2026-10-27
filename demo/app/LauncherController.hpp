#pragma once

#include <cstdint>

#include "host/Recent.hpp"

namespace burnhope {

struct Engine;

struct LauncherState {
    RecentList recent{};
    char recentFile[512]{};
    uint8_t asking = 0; // 0 idle, 1 waiting for the folder of a new project, 2 waiting for the folder of an existing one
};

// Mounts assets/gui/launcher.json and shows the recent-project modal at startup.
[[nodiscard]] bool launcherBind(Engine& e);
// Main thread, once per frame: picks up the folder the native dialog returned.
void launcherSync(Engine& e);
void launcherShow(Engine& e);

} // namespace burnhope

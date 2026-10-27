#pragma once

#include <cstdint>
#include <filesystem>

#include "host/Recent.hpp"

namespace burnhope {

// Click action ids of assets/gui/launcher.json and the File menu. Ids are unique across the app because the JSON addresses them by number.
enum LauncherAction : uint32_t {
    kActFileProjects = 24,
    kActLauncherCreate = 190,
    kActLauncherOpen = 191,
    kActLauncherRecent = 192, // 192..199 open the n-th recent project
    kActLauncherForget = 200, // 200..207 drop the n-th recent project
    kActLauncherClose = 208,
};

struct GuiHost;
struct Window;

// Roots the file browser at the opened project folder.
using LauncherOpenFn = void (*)(void* app, const std::filesystem::path& dir);

struct LauncherState {
    RecentList recent{};
    char recentFile[512]{};
    GuiHost* gui = nullptr; // set by launcherBind
    void* app = nullptr;
    LauncherOpenFn openRoot = nullptr;
    Window* window = nullptr; // the window the folder dialogs belong to, refreshed by launcherSync
    uint8_t asking = 0; // 0 idle, 1 waiting for the folder of a new project, 2 waiting for the folder of an existing one
};

// Mounts assets/gui/launcher.json and shows the recent-project modal at startup.
[[nodiscard]] bool launcherBind(LauncherState& l, GuiHost& gui, void* app, LauncherOpenFn openRoot);
// Main thread, once per frame: picks up the folder the native dialog returned.
void launcherSync(LauncherState& l, Window* window);
void launcherShow(LauncherState& l);

} // namespace burnhope

#pragma once

#include "host/Recent.hpp"
#include "ui/Types.hpp"

namespace burnhope {

struct LauncherConfig {
    void (*onCreate)(void* user) = nullptr;
    void (*onOpen)(void* user) = nullptr;
    // A recent project was clicked (index into the list last given to uiLauncherList) / its "x" was clicked.
    void (*onRecent)(void* user, uint32_t index) = nullptr;
    void (*onForget)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
};

struct UiLauncher {
    LauncherConfig cfg{};
    uint16_t empty = kUiNone;
    uint16_t row[kRecentCap]{};
};

struct UiLauncherRow {
    uint16_t launcher = kUiNone;
    uint16_t name = kUiNone;
    uint16_t path = kUiNone;
    uint16_t date = kUiNone;
    uint16_t forget = kUiNone;
    uint32_t index = 0;
};

// A full-window veil with a centered card: Create Project, Open Project and the recent list. Hidden until shown.
uint16_t uiLauncher(UiState& s, const LauncherConfig& cfg);
// Fills the recent rows from `list` and hides the unused ones.
void uiLauncherList(UiState& s, uint16_t launcher, const RecentList& list);
void uiLauncherShow(UiState& s, uint16_t launcher, bool on);

} // namespace burnhope

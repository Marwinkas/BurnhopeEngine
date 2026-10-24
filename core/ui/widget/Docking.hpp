#pragma once

#include "ui/State.hpp"
#include "ui/widget/Chrome.hpp"

namespace burnhope {

struct UiDock {
    uint16_t home = kUiNone;
    uint16_t panel = kUiNone;
    uint8_t edges = 15;
    uint8_t edge = 0;
    uint8_t hintOn = 0;
    uint8_t edgeOn = 0;
    UiBox hint{};
    UiBox edgeLine{};
};

inline UiDock* uiDockFind(UiState& s) {
    const uint16_t id = uiMarked<UiDock>(s);
    return id < s.count ? s.ent[id].try_get_mut<UiDock>() : nullptr;
}

inline const UiDock* uiDockFind(const UiState& s) {
    const uint16_t id = uiMarked<UiDock>(s);
    return id < s.count ? s.ent[id].try_get<UiDock>() : nullptr;
}

inline void uiDockBind(UiState& s, uint16_t home, uint16_t panel, uint8_t edges) {
    if (home >= s.count) {
        return;
    }
    const uint16_t prev = uiMarked<UiDock>(s);
    UiDock dock{};
    if (prev == home) {
        if (const UiDock* live = s.ent[home].try_get<UiDock>()) {
            dock = *live;
        }
    } else if (prev < s.count) {
        s.ent[prev].remove<UiDock>();
    }
    dock.home = home;
    dock.panel = panel;
    dock.edges = edges;
    s.ent[home].set<UiDock>(dock);
}

void uiDockTo(UiState& s, uint8_t edge);
void uiInstallDockOverlay(UiState& s);
bool uiDockInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope

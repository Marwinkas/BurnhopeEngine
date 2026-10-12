#pragma once

#include "ui/State.hpp"

#include <flecs.h>

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

struct UiHostNote {
    bool tool = false;
    bool color = false;
    bool file = false;
};

struct UiPick {
    uint16_t id = kUiNone;
    char name[32]{};
    uint8_t len = 0;
};

void uiClearDomain(UiState& s);

inline UiDock& uiDock(UiState& s) {
    return s.world.ensure<UiDock>();
}
inline UiHostNote& uiHostNote(UiState& s) {
    return s.world.ensure<UiHostNote>();
}
inline UiPick& uiPick(UiState& s) {
    return s.world.ensure<UiPick>();
}

} // namespace burnhope

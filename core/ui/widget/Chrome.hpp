#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

#include <type_traits>

namespace burnhope {

struct UiTitleBar {};
struct UiResizable {};

struct UiChromeGesture {
    uint16_t floatWin = kUiNone;
    uint16_t resizeId = kUiNone;
    uint8_t resizeEdge = 0;
};

struct ChromePointer {
    uint8_t cursor = 0;
    uint8_t took = 0;
};

static_assert(std::is_trivially_copyable_v<UiChromeGesture>);
static_assert(std::is_trivially_copyable_v<ChromePointer>);

inline UiChromeGesture* uiChromeFind(UiState& s) {
    const uint16_t id = uiMarked<UiChromeGesture>(s);
    return id < s.count ? s.ent[id].try_get_mut<UiChromeGesture>() : nullptr;
}

inline void uiChromeClear(UiState& s) {
    const uint16_t id = uiMarked<UiChromeGesture>(s);
    if (id < s.count && s.ent[id].is_alive()) {
        s.ent[id].remove<UiChromeGesture>();
    }
}

inline void uiChromePark(UiState& s, uint16_t home, const UiChromeGesture& gesture) {
    if (home >= s.count) {
        uiChromeClear(s);
        return;
    }
    const uint16_t prev = uiMarked<UiChromeGesture>(s);
    if (prev < s.count && prev != home && s.ent[prev].is_alive()) {
        s.ent[prev].remove<UiChromeGesture>();
    }
    s.ent[home].set<UiChromeGesture>(gesture);
}

uint16_t uiWindowChrome(UiState& s, uint16_t parent);
void uiInstallChromeBin(UiState& s);
void uiChromeSetDock(UiState& s, bool (*frame)(const UiState& s, uint16_t id), void (*move)(UiState& s, uint16_t panel, float x, float y), void (*drop)(UiState& s, uint16_t panel));
ChromePointer uiChromePointer(UiState& s, const InputFrame& in, uint16_t hit, bool press, bool down, bool release);

} // namespace burnhope

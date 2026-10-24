#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiCalendarComponent {
    int calY = 2026;
    int calM = 10;
    int calSel = 0;
    uint16_t calPanel = kUiNone;
    uint16_t calHead = kUiNone;
    uint16_t calTarget = kUiNone;
};

struct UiCalDay {
    uint8_t cell = 0;
};

inline UiCalendarComponent* uiCalFind(UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (!s.ent[i].is_alive() || !s.ent[i].has<UiCalendarComponent>()) {
            continue;
        }
        return s.ent[i].try_get_mut<UiCalendarComponent>();
    }
    return nullptr;
}

inline const UiCalendarComponent* uiCalFind(const UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (!s.ent[i].is_alive() || !s.ent[i].has<UiCalendarComponent>()) {
            continue;
        }
        return s.ent[i].try_get<UiCalendarComponent>();
    }
    return nullptr;
}

inline UiCalendarComponent* uiCal(UiState& s) {
    return uiCalFind(s);
}

inline const UiCalendarComponent* uiCal(const UiState& s) {
    return uiCalFind(s);
}

void uiInstallCalendarBin(UiState& s);
void calOnOpen(void* user, uint16_t id);

} // namespace burnhope

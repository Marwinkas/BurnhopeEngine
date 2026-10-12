#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiCalendarComponent {
    int calY = 2026;
    int calM = 10;
    int calSel = 0;
    uint16_t calPanel = kUiNone;
    uint16_t calHead = kUiNone;
    uint16_t calDay[42]{};
    uint16_t calTarget = kUiNone;
};

inline UiCalendarComponent& uiCal(UiState& s) {
    return s.world.ensure<UiCalendarComponent>();
}
void uiInstallCalendarBin(UiState& s);
void calOnOpen(void* user, uint16_t id, int16_t tag);

inline const UiCalendarComponent& uiCal(const UiState& s) {
    return *s.world.try_get<UiCalendarComponent>();
}

} // namespace burnhope

#pragma once

#include "ui/Types.hpp"
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

UiCalendarComponent* uiCalFind(UiState& s);

const UiCalendarComponent* uiCalFind(const UiState& s);

inline UiCalendarComponent* uiCal(UiState& s) {
    return uiCalFind(s);
}

inline const UiCalendarComponent* uiCal(const UiState& s) {
    return uiCalFind(s);
}

// The field the calendar popup writes its date into. Does nothing without a calendar in the state.
void uiCalSetTarget(UiState& s, uint16_t field);
void uiInstallCalendarBin(UiState& s);
void calOnOpen(void* user, uint16_t id);

} // namespace burnhope

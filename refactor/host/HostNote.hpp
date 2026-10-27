#pragma once

#include "ui/Types.hpp"

namespace burnhope {

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

UiHostNote& uiHostNote(UiState& s);
UiPick& uiPick(UiState& s);

} // namespace burnhope

#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiFoldState {
    float openH = 0.0f;
};

void uiFold(UiState& s, uint16_t id);

} // namespace burnhope

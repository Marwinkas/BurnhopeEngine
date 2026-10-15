#pragma once

#include "ui/Metrics.hpp"
#include "ui/State.hpp"

namespace burnhope {

// Photo slot when one is cached, otherwise the procedural icon. No fill of its own.
void uiThumb(UiState& s, uint16_t id, uint8_t photoSlot, UiIcon fallback);

} // namespace burnhope

#pragma once

#include "ui/Types.hpp"

#include <cstddef>

namespace burnhope {

struct HudContext;

UiState* matPageState(HudContext& hud);
void matApplyFlag(HudContext& hud, UiState& s, uint16_t index, uint8_t value);
uint16_t matResetBtn(UiState& s, HudContext& hud, uint16_t parent, uint16_t index, float home, float home2, uint8_t kind);
void matAddSlot(UiState& s, HudContext& hud, uint16_t panel, uint8_t slot);
void matOnClear(void* user, uint16_t id);
void matOnPath(void* user, const char* path);

} // namespace burnhope

#pragma once

#include "ui/Types.hpp"

#include <cstddef>

namespace burnhope {

struct HudContext;
struct UiResetHome;

UiState* matPageState(HudContext& hud);
void matApplyFlag(HudContext& hud, UiState& s, uint16_t index, uint8_t value);
// Binder reset hook: runs the side effects of a restored flag, texture or colour row, then repaints and saves.
void matOnReset(void* user, UiState& s, const UiResetHome& home);
void matAddSlot(UiState& s, HudContext& hud, uint16_t panel, uint8_t slot);
void matOnClear(void* user, uint16_t id);
void matOnPath(void* user, const char* path);

} // namespace burnhope

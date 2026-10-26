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
struct PropSpec;
struct UiPropBinder;
// PropRole "matslot": a property row with the texture thumbnail and the slot's colour or slider.
uint16_t matSlotRole(void* user, UiState& s, UiPropBinder& bind, uint16_t panel, const PropSpec& spec);
void matOnClear(void* user, uint16_t id);
void matOnPath(void* user, const char* path);

} // namespace burnhope

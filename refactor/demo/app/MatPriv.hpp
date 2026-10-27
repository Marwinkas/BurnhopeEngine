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
// PropRole "matrow": a compact row of the left column (flag check, Tiling / Offset pair, or Type / Maps choice).
uint16_t matRowRole(void* user, UiState& s, UiPropBinder& bind, uint16_t panel, const PropSpec& spec);
// A bare check bound to `flag`, and a compact number field bound to `scalar`.
uint16_t matFlagCheck(UiState& s, UiPropBinder& bind, uint16_t parent, const char* label, float labelW, uint16_t flag, bool on);
uint16_t matNumber(UiState& s, UiPropBinder& bind, uint16_t parent, uint16_t scalar, float def, float lo, float hi, float step, float width);
void matOnClear(void* user, uint16_t id);
void matOnPath(void* user, const char* path);

} // namespace burnhope

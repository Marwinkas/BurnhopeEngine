#pragma once

#include "ui/Types.hpp"

#include <cstddef>

namespace burnhope {

struct HudContext;

struct MatHex {
    HudContext* hud = nullptr;
    uint16_t index = 0;
    uint16_t chip = kUiNone;
};

UiState* matPageState(HudContext& hud);
void matSetChoice(UiState& s, uint16_t id, uint8_t index, const char* label);
void matApplyFlag(HudContext& hud, UiState& s, uint16_t index, uint8_t value);
void matHexText(char* dst, std::size_t cap, float r, float g, float b, float a, bool withA);
uint16_t matResetBtn(UiState& s, HudContext& hud, uint16_t parent, uint16_t index, float home, float home2, uint8_t kind);
void matAddSlot(UiState& s, HudContext& hud, uint16_t panel, uint8_t slot);
void matOnSlide(void* user, float value);
void matOnNumber(void* user, float value);
void matHexEdited(UiState& s, uint16_t id);
void matOnClear(void* user, uint16_t id);
void matOnPath(void* user, const char* path);

} // namespace burnhope

#pragma once

#include "ui/Interaction.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiState;

struct UiCheck {
    uint8_t on = 0;
    uint8_t kind = 0;
    uint8_t group = 0;
};

uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind = 0, uint8_t group = 0, bool on = false);
void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on);
void uiCheckAssign(UiState& s, uint16_t id, uint8_t on);
void uiCheckPress(UiState& s, uint16_t id);
int uiCheckOn(const UiState& s, const char* name);
int uiCheckOn(const UiState& s, uint16_t id);
void uiCheckBin(UiState& s);

} // namespace burnhope

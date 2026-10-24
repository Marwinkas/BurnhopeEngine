#pragma once

#include "ui/widget/BinSlots.hpp"
#include "ui/widget/Field.hpp"

namespace burnhope {

uint16_t uiSearchField(UiState& s, uint16_t parent, const char* hint, UiTextChangeFn onChange, void* user, float width = 160.0f);
void uiBindSearch(UiState& s, const char* name, UiTextChangeFn onChange, void* user);
void uiSearchBin(UiState& s);

} // namespace burnhope

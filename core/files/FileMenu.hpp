#pragma once

#include "ui/Types.hpp"

namespace burnhope {

void fileMenuOpen(UiState& s, float x, float y, bool onItem);
void fileOnContext(void* user, uint32_t index, float x, float y);

} // namespace burnhope

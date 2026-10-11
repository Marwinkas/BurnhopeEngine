#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user);
void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path);

} // namespace burnhope

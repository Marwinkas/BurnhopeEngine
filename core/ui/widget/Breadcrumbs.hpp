#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

struct UiCrumbs {
    uint8_t n = 0;
    void (*nav)(void* user, uint16_t index) = nullptr;
    void* user = nullptr;
};

void uiCrumbsBin(UiState& s);

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, uint16_t index), void* user);
void uiBindCrumbs(UiState& s, const char* name, void (*nav)(void* user, uint16_t index), void* user);
void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* const* labels, uint8_t count);

} // namespace burnhope

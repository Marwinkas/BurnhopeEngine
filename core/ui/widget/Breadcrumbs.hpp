#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiCrumbs {
    uint16_t seg[8]{};
    uint16_t sep[7]{};
    uint8_t n = 0;
    char path[512]{};
    void (*nav)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user);
void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path);

} // namespace burnhope

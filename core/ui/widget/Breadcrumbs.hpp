#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiCrumbs {
    uint8_t n = 0;
    char root[512]{};
    void (*nav)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

inline constexpr uint8_t kBinSlotCrumbs = 18;
void uiCrumbsBin(UiState& s);

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user);
void uiBindCrumbs(UiState& s, const char* name, void (*nav)(void* user, const char* path), void* user);
void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path, const char* root = nullptr);

} // namespace burnhope

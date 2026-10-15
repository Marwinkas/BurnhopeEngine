#pragma once

#include "ui/State.hpp"

namespace burnhope {

inline constexpr uint8_t kUiCrumbCap = 32;

struct UiCrumbs {
    uint16_t seg[kUiCrumbCap]{};
    uint16_t sep[kUiCrumbCap]{};
    uint8_t n = 0;
    uint8_t cap = 0;
    char path[512]{};
    char root[512]{};
    void (*nav)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user);
void uiBindCrumbs(UiState& s, const char* name, void (*nav)(void* user, const char* path), void* user);
void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path, const char* root = nullptr);

} // namespace burnhope

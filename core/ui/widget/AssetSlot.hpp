#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiAssetSlot {
    uint16_t caption = kUiNone;
    void (*onPath)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

struct UiAssetSlotConfig {
    const char* label = nullptr;
    const char* value = nullptr;
    void (*onBrowse)(void* user, uint16_t id) = nullptr;
    void (*onClear)(void* user, uint16_t id) = nullptr;
    void (*onPath)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

uint16_t uiAssetSlot(UiState& s, uint16_t parent, const UiAssetSlotConfig& cfg);
void uiAssetSlotText(UiState& s, uint16_t id, const char* text);

} // namespace burnhope

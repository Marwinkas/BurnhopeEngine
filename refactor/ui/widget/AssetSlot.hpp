#pragma once

#include "ui/Types.hpp"

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

// Square texture drop slot: a framed atlas-cell preview with a "None" caption and a clear button.
struct UiTexSlotConfig {
    float side = 64.0f;
    const char* emptyLabel = "None";
    uint8_t photo = 0;      // UiPhoto slot holding the atlas
    uint8_t atlasCols = 1;
    uint8_t atlasRows = 1;
    uint8_t cell = 0;       // row-major atlas cell shown in the preview
    void (*onBrowse)(void* user, uint16_t id) = nullptr;
    void (*onClear)(void* user, uint16_t id) = nullptr;
    void (*onPath)(void* user, const char* path) = nullptr;
    void* user = nullptr;
};

struct UiTexSlotIds {
    uint16_t frame = kUiNone;
    uint16_t empty = kUiNone;
    uint16_t thumb = kUiNone;
    uint16_t clear = kUiNone;
};

UiTexSlotIds uiTexSlot(UiState& s, uint16_t parent, const UiTexSlotConfig& cfg);
uint16_t uiAssetSlot(UiState& s, uint16_t parent, const UiAssetSlotConfig& cfg);
void uiAssetSlotText(UiState& s, uint16_t id, const char* text);
bool uiAssetOfferPath(UiState& s, const char* path);

} // namespace burnhope

#pragma once

#include "ui/widget/Virtual.hpp"

namespace burnhope {

struct UiVirtualGrid : UiVirtualBase {
    float itemW = 84.0f;
    uint8_t cols = 1;
    uint8_t list = 0;
    void (*bind)(void* user, uint16_t cell, uint32_t index) = nullptr;
    void (*select)(void* user, uint32_t index, bool multi) = nullptr;
    void (*activate)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
    uint32_t lastClickIndex = 0xFFFFFFFFu;
    uint16_t lastClickId = kUiNone;
    float lastClickTime = 0.0f;
    float lastClickX = 0.0f;
    float lastClickY = 0.0f;
    float pressX = 0.0f;
    float pressY = 0.0f;
};

struct UiVirtualGridConfig {
    float itemW = 84.0f;
    float itemH = 96.0f;
    uint32_t totalItems = 0;
    void (*onBindCell)(void* user, uint16_t cellId, uint32_t itemIndex) = nullptr;
    void (*onSelect)(void* user, uint32_t itemIndex, bool multi) = nullptr;
    void (*onActivate)(void* user, uint32_t itemIndex) = nullptr;
    void* user = nullptr;
};

uint16_t uiVirtualGrid(UiState& s, uint16_t parent, const UiVirtualGridConfig& cfg);
void uiVirtualGridRefresh(UiState& s, uint16_t gridId);
void uiVirtualGridClearClick(UiState& s, uint16_t gridId);
bool uiVirtualGridInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope

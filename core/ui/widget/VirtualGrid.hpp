#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiVirtualGrid {
    float itemW = 84.0f;
    float itemH = 96.0f;
    uint32_t count = 0;
    uint32_t first = 0;
    uint8_t cols = 1;
    uint8_t slots = 0;
    uint16_t slot[kUiPoolCap]{};
    void (*bind)(void* user, uint16_t cell, uint32_t index) = nullptr;
    void (*select)(void* user, uint32_t index, bool multi) = nullptr;
    void (*activate)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
    uint32_t last = 0xFFFFFFFFu;
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

} // namespace burnhope

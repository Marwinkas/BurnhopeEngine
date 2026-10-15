#pragma once

#include "ui/widget/Virtual.hpp"

namespace burnhope {

inline constexpr uint8_t kUiCellPlain = 0;
inline constexpr uint8_t kUiCellSelected = 1;
inline constexpr uint8_t kUiCellHeader = 2;
inline constexpr uint32_t kUiCellNone = 0xFFFFFFFFu;

struct UiVirtualGrid : UiVirtualBase {
    float itemW = 84.0f;
    uint8_t cols = 1;
    uint8_t list = 0;
    void (*bind)(void* user, uint16_t cell, uint32_t index) = nullptr;
    void (*select)(void* user, uint32_t index, bool multi) = nullptr;
    void (*activate)(void* user, uint32_t index) = nullptr;
    uint8_t (*role)(void* user, uint32_t index) = nullptr;
    void (*hover)(void* user, uint32_t index) = nullptr;
    void (*context)(void* user, uint32_t index, float x, float y) = nullptr;
    void (*empty)(void* user) = nullptr;
    void (*zoom)(void* user, float wheel) = nullptr;
    void (*commit)(void* user, uint32_t index, const char* text) = nullptr;
    void (*cancel)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
    uint16_t editor = kUiNone;
    uint32_t editAt = kUiCellNone;
    uint32_t hoverAt = kUiCellNone;
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
    uint8_t (*onRole)(void* user, uint32_t itemIndex) = nullptr;
    void (*onHover)(void* user, uint32_t itemIndex) = nullptr;
    void (*onContext)(void* user, uint32_t itemIndex, float x, float y) = nullptr;
    void (*onEmptyClick)(void* user) = nullptr;
    void (*onZoom)(void* user, float wheel) = nullptr;
    void (*onCommit)(void* user, uint32_t itemIndex, const char* text) = nullptr;
    void (*onCancel)(void* user, uint32_t itemIndex) = nullptr;
    void* user = nullptr;
};

uint16_t uiVirtualGrid(UiState& s, uint16_t parent, const UiVirtualGridConfig& cfg);
void uiBindGrid(UiState& s, const char* name, const UiVirtualGridConfig& cfg);
void uiVirtualGridRefresh(UiState& s, uint16_t gridId);
void uiVirtualGridSetView(UiState& s, uint16_t gridId, uint32_t count, uint8_t list, float itemW, float itemH);
void uiVirtualGridClearClick(UiState& s, uint16_t gridId);
void uiVirtualGridBeginEdit(UiState& s, uint16_t gridId, uint32_t index, const char* text);
bool uiVirtualGridEditing(const UiState& s, uint16_t gridId);
bool uiVirtualGridInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

} // namespace burnhope

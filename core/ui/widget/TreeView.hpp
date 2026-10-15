#pragma once

#include "ui/widget/Virtual.hpp"

namespace burnhope {

struct UiTreeView : UiVirtualBase {
    void (*bind)(void* user, uint16_t row, uint32_t index) = nullptr;
    void (*toggle)(void* user, uint32_t index) = nullptr;
    void (*select)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
};

struct UiParts {
    uint16_t icon = kUiNone;
    uint16_t chevron = kUiNone;
    uint16_t label = kUiNone;
};

struct UiTree {
    uint16_t body = kUiNone;
    uint8_t open = 0;
};

struct UiTreeConfig {
    uint32_t totalNodes = 0;
    void (*onBindNode)(void* user, uint16_t rowEntity, uint32_t nodeIndex) = nullptr;
    void (*onToggleNode)(void* user, uint32_t nodeIndex) = nullptr;
    void (*onSelectNode)(void* user, uint32_t nodeIndex) = nullptr;
    void* user = nullptr;
};

uint16_t uiTreeView(UiState& s, uint16_t parent, const UiTreeConfig& cfg);
void uiBindTree(UiState& s, const char* name, const UiTreeConfig& cfg);
void uiTreeViewRefresh(UiState& s, uint16_t treeId);
void uiRowIndent(UiState& s, uint16_t id, float pad);
uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text);

} // namespace burnhope

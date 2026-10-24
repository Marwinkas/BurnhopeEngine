#pragma once

#include "ui/widget/BinSlots.hpp"
#include "ui/widget/Virtual.hpp"

namespace burnhope {

struct UiTreeView : UiVirtualBase {
    void (*bind)(void* user, uint16_t row, uint32_t index) = nullptr;
    void (*toggle)(void* user, uint32_t index) = nullptr;
    void (*select)(void* user, uint32_t index) = nullptr;
    uint8_t (*depth)(void* user, uint32_t index) = nullptr;
    uint8_t (*open)(void* user, uint32_t index) = nullptr;
    void (*empty)(void* user) = nullptr;
    void* user = nullptr;
};

struct UiTreeConfig {
    uint32_t totalNodes = 0;
    void (*onBindNode)(void* user, uint16_t rowEntity, uint32_t nodeIndex) = nullptr;
    void (*onToggleNode)(void* user, uint32_t nodeIndex) = nullptr;
    void (*onSelectNode)(void* user, uint32_t nodeIndex) = nullptr;
    uint8_t (*onDepth)(void* user, uint32_t nodeIndex) = nullptr;
    uint8_t (*onOpen)(void* user, uint32_t nodeIndex) = nullptr;
    void (*onEmptyClick)(void* user) = nullptr;
    void* user = nullptr;
};

void uiTreeBin(UiState& s);

uint16_t uiTreeView(UiState& s, uint16_t parent, const UiTreeConfig& cfg);
void uiBindTree(UiState& s, const char* name, const UiTreeConfig& cfg);
void uiTreeViewRefresh(UiState& s, uint16_t treeId);
void uiTreeViewSetCount(UiState& s, uint16_t treeId, uint32_t count);
bool uiTreeInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope

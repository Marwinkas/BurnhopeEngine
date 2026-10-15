#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiSplitter {
    uint16_t pane = kUiNone;
    uint8_t useNext = 0;
    uint8_t axis = 0;
    uint8_t armed = 0;
    float startSize = 0.0f;
    float startMouse = 0.0f;
};

struct UiSplitShare {};

struct UiPropLabel {};

uint16_t splitTarget(const UiState& s, uint16_t split);
void splitDrag(UiState& s, uint16_t split, float x, float y);
bool splitAlive(const UiState& s, uint16_t split);
uint8_t splitCursor(const UiState& s, uint16_t split);
bool splitVertical(const UiState& s, uint16_t split);
UiBox splitGrip(const UiState& s, uint16_t split);
void splitSync(UiState& s, uint16_t hot);
bool uiSplitInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);
uint16_t uiSplitBar(UiState& s, uint16_t parent, bool verticalBar);

} // namespace burnhope

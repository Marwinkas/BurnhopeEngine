#pragma once

#include "platform/Input.hpp"
#include "ui/Interaction.hpp"
#include "ui/Types.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

using UiSplitResizeFn = float (*)(void* user, uint16_t split, float size);

struct UiSplitter {
    uint16_t pane = kUiNone;
    uint16_t target = kUiNone;
    uint8_t useNext = 0;
    uint8_t axis = 0;
    uint8_t axisLock = 0;
    uint8_t armed = 0;
    float startSize = 0.0f;
    float startMouse = 0.0f;
    float live = 0.0f;
    UiSplitResizeFn onResize = nullptr;
    void* resizeUser = nullptr;
};

uint16_t splitTarget(const UiState& s, uint16_t split);
void uiSplitterBind(UiState& s, uint16_t split, uint16_t target, uint8_t axis, UiSplitResizeFn fn, void* user);
void splitDrag(UiState& s, uint16_t split, float x, float y);
bool splitVertical(const UiState& s, uint16_t split);
bool uiSplitInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);
void uiSplitBin(UiState& s);

uint16_t uiSplitBar(UiState& s, uint16_t parent, bool verticalBar);
void uiSplitterSetWidth(UiState& s, uint16_t split, float width);

} // namespace burnhope

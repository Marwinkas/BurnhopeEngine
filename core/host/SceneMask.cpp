#include "host/SceneMask.hpp"

#include "ui/State.hpp"

#include <cstring>

namespace burnhope {

void sceneMaskInput(const UiState& s, InputFrame& input) {
    if (s.hovered != kUiNone || s.capture != kUiNone) {
        input.pointer.dx = 0.0f;
        input.pointer.dy = 0.0f;
        input.pointer.down = 0;
        input.pointer.pressed = 0;
        input.pointer.released = 0;
        input.clicks = 0;
    }
    if (s.ateWheel != 0) {
        input.wheel = 0.0f;
        input.wheelX = 0.0f;
    }
    const UiText* focusedText = s.focused < s.count ? s.ent[s.focused].try_get<UiText>() : nullptr;
    if (s.focused != kUiNone && focusedText != nullptr && focusedText->ownRun != 0) {
        input.textLen = 0;
        input.backspace = 0;
        input.del = 0;
        input.enter = 0;
        input.tab = 0;
        input.left = 0;
        input.right = 0;
        input.up = 0;
        input.down = 0;
        std::memset(input.keyDown, 0, sizeof(input.keyDown));
        std::memset(input.keyEdge, 0, sizeof(input.keyEdge));
        std::memset(input.keyUp, 0, sizeof(input.keyUp));
    }
}

} // namespace burnhope

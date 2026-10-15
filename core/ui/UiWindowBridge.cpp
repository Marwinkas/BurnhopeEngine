#include "ui/UiWindowBridge.hpp"
#include "ui/State.hpp"
#include <cstring>

namespace burnhope {

void uiWindowTakeOps(UiState& s, WindowOps& out) {
    out = {};
    out.close = s.opClose;
    out.minimize = s.opMinimize;
    out.toggleMaximize = s.opMaximize;
    out.alwaysOnTop = s.opTop;
    out.opacity = s.opOpacity;

    s.opClose = false;
    s.opMinimize = false;
    s.opMaximize = false;
    s.opTop = -1;
    s.opOpacity = -1.0f;
}

bool uiWindowTakeClip(UiState& s, char* dst, uint8_t cap) {
    if (s.clipPut == 0 || dst == nullptr || cap == 0) {
        return false;
    }
    const uint8_t n = s.clipLen < static_cast<uint8_t>(cap - 1) ? s.clipLen : static_cast<uint8_t>(cap - 1);
    for (uint8_t i = 0; i < n; ++i) {
        dst[i] = s.clip[i];
    }
    dst[n] = '\0';
    s.clipPut = 0;
    return n > 0;
}

void uiWindowChromeMetrics(const UiLook& look, float& titleBar, float& resizeBorder) {
    titleBar = look.titleBar;
    resizeBorder = look.resizeBorder;
}

void uiMaskSceneInput(const UiState& s, InputFrame& input) {
    // 1. Если мышь над элементами UI или идет перетаскивание — блокируем клики и вращение камеры
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

    // 2. Если элемент с фокусом принимает ввод текста — глушим клавиатуру для 3D-сцены
    if (s.focused != kUiNone && s.focused < s.count && s.ent[s.focused].has<UiTextInput>()) {
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
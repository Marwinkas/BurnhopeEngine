#include "ui/UiWindowBridge.hpp"
#include "ui/State.hpp"

namespace burnhope {

void uiRequestTop(UiState& s, bool on) {
    s.world.ensure<WindowOps>().alwaysOnTop = on ? 1 : 0;
}

void uiRequestOpacity(UiState& s, float opacity) {
    s.world.ensure<WindowOps>().opacity = opacity;
}

void uiRequestMinimize(UiState& s) {
    s.world.ensure<WindowOps>().minimize = true;
}

void uiRequestMaximize(UiState& s) {
    s.world.ensure<WindowOps>().toggleMaximize = true;
}

void uiRequestClose(UiState& s) {
    s.world.ensure<WindowOps>().close = true;
}

void uiWindowTakeOps(UiState& s, WindowOps& out) {
    out = {};
    WindowOps* ops = s.world.try_get_mut<WindowOps>();
    if (ops == nullptr) {
        return;
    }
    out = *ops;
    *ops = {};
}

bool uiWindowTakeClip(UiState& s, char* dst, uint8_t cap) {
    UiClipOut* clip = s.world.try_get_mut<UiClipOut>();
    if (clip == nullptr || clip->put == 0 || dst == nullptr || cap == 0) {
        return false;
    }
    const uint8_t n = clip->len < static_cast<uint8_t>(cap - 1) ? clip->len : static_cast<uint8_t>(cap - 1);
    for (uint8_t i = 0; i < n; ++i) {
        dst[i] = clip->bytes[i];
    }
    dst[n] = '\0';
    clip->put = 0;
    return n > 0;
}

void uiWindowChromeMetrics(const UiLook& look, float& titleBar, float& resizeBorder) {
    titleBar = look.titleBar;
    resizeBorder = look.resizeBorder;
}

} // namespace burnhope
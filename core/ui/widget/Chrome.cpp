#include "ui/Geometry.hpp"
#include "ui/Icon.hpp"
#include "ui/UiBin.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Chrome.hpp"

namespace burnhope {
namespace {

void onMin(void* user, uint16_t) {
    uiRequestMinimize(*static_cast<UiState*>(user));
}

void onMax(void* user, uint16_t) {
    uiRequestMaximize(*static_cast<UiState*>(user));
}

} // namespace

uint16_t uiWindowChrome(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.widthMode = UiSize::Px;
    row.width = 100.0f;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    row.justify = UiJustify::Center;
    row.gap = 4.0f;
    row.marginR = 8.0f;
    const uint16_t cluster = uiPanel(s, parent, row, paintClear());
    const uint16_t minB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t maxB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Chrome, s.look.radiusSm));
    const uint16_t closeB = uiPanel(s, cluster, px(28.0f, 28.0f), paintToken(s.look, UiToken::Close, s.look.radiusSm));
    uiHover(s, minB);
    uiHover(s, maxB);
    uiHover(s, closeB);
    if (auto* icon = s.ent[minB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Minimize);
    }
    if (auto* icon = s.ent[maxB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Maximize);
    }
    if (auto* icon = s.ent[closeB].try_get_mut<UiPaint>()) {
        icon->icon = static_cast<uint8_t>(UiIcon::Dismiss);
    }
    uiBind(s, minB, onMin, &s);
    uiBind(s, maxB, onMax, &s);
    return cluster;
}

uint16_t loadChromeBin(UiState& s, uint16_t parent, const BinNode&, const BinTail&, const char* name, const char*, const char*, void*) {
    const uint16_t id = uiWindowChrome(s, parent);
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    return id;
}

void uiInstallChromeBin(UiState& s) {
    uiBinRole(s, kBinSlotChrome, "chrome", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotChrome, loadChromeBin, nullptr);
}

void uiChromeSetDock(UiState& s, bool (*frame)(const UiState& s, uint16_t id), void (*move)(UiState& s, uint16_t panel, float x, float y), void (*drop)(UiState& s, uint16_t panel)) {
    s.plug.frame = frame;
    s.plug.move = move;
    s.plug.drop = drop;
}

ChromePointer uiChromePointer(UiState& s, const InputFrame& in, uint16_t hit, bool press, bool down, bool release) {
    ChromePointer out{};
    auto edgeAt = [](const UiBox& b, float x, float y) {
        const float m = 6.0f;
        if (!hitBox(b, x, y)) {
            return 0;
        }
        const bool left = x < b.x + m;
        const bool right = x > b.x + b.w - m;
        const bool top = y < b.y + m;
        const bool bottom = y > b.y + b.h - m;
        if (!left && !right && !top && !bottom) {
            return 0;
        }
        return (left ? 1 : 0) | (right ? 2 : 0) | (top ? 4 : 0) | (bottom ? 8 : 0);
    };
    UiChromeGesture gesture{};
    if (const UiChromeGesture* live = uiChromeFind(s)) {
        gesture = *live;
    }
    const UiPlug& hook = s.plug;
    int hoverEdge = 0;
    uint16_t frame = kUiNone;
    if (gesture.resizeId == kUiNone) {
        for (uint16_t n = s.count; n > 0; --n) {
            const uint16_t id = static_cast<uint16_t>(n - 1);
            if (!uiIsOverlay(s, id) || s.yoga[id] == nullptr || !nodeShown(s, id)) {
                continue;
            }
            const bool frameNode = s.ent[id].has<UiResizable>();
            const bool floating = hook.frame != nullptr && hook.frame(s, id);
            if (!frameNode && !floating) {
                continue;
            }
            const int edge = edgeAt(s.box[id], in.pointer.x, in.pointer.y);
            if (edge == 0) {
                continue;
            }
            frame = id;
            hoverEdge = edge;
            break;
        }
    } else {
        frame = gesture.resizeId;
        hoverEdge = gesture.resizeEdge;
    }
    if (hoverEdge != 0) {
        const bool across = (hoverEdge & 3) != 0;
        const bool downEdge = (hoverEdge & 12) != 0;
        if (across && downEdge) {
            const bool nw = ((hoverEdge & 1) != 0 && (hoverEdge & 4) != 0) || ((hoverEdge & 2) != 0 && (hoverEdge & 8) != 0);
            out.cursor = nw ? 4 : 3;
        } else if (across) {
            out.cursor = 1;
        } else {
            out.cursor = 2;
        }
    }
    if (press && hoverEdge != 0 && frame != kUiNone) {
        s.capture = frame;
        gesture.resizeId = frame;
        gesture.resizeEdge = static_cast<uint8_t>(hoverEdge);
        s.grabX = in.pointer.x;
        s.grabY = in.pointer.y;
        out.took = 1;
    } else if (press && hit != kUiNone && s.ent[hit].has<UiTitleBar>() && s.parentOf[hit] != kUiNone) {
        const uint16_t panel = s.parentOf[hit];
        s.capture = hit;
        gesture.floatWin = panel;
        s.grabX = in.pointer.x - s.box[panel].x;
        s.grabY = in.pointer.y - s.box[panel].y;
        out.took = 1;
    }
    if (down && gesture.resizeId != kUiNone && gesture.resizeId < s.count && s.capture == gesture.resizeId) {
        auto* flex = s.ent[gesture.resizeId].try_get_mut<UiFlex>();
        if (flex != nullptr) {
            float x = flex->posX;
            float y = flex->posY;
            float w = s.box[gesture.resizeId].w;
            float h = s.box[gesture.resizeId].h;
            const float dx = in.pointer.x - s.grabX;
            const float dy = in.pointer.y - s.grabY;
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
            if ((gesture.resizeEdge & 1) != 0) {
                x += dx;
                w -= dx;
            }
            if ((gesture.resizeEdge & 2) != 0) {
                w += dx;
            }
            if ((gesture.resizeEdge & 4) != 0) {
                y += dy;
                h -= dy;
            }
            if ((gesture.resizeEdge & 8) != 0) {
                h += dy;
            }
            if (w < 220.0f) {
                if ((gesture.resizeEdge & 1) != 0) {
                    x -= 220.0f - w;
                }
                w = 220.0f;
            }
            if (h < 120.0f) {
                if ((gesture.resizeEdge & 4) != 0) {
                    y -= 120.0f - h;
                }
                h = 120.0f;
            }
            uiPlace(s, gesture.resizeId, true, x, y, w, h);
        }
        out.took = 1;
    } else if (down && s.capture != kUiNone && s.capture < s.count && s.ent[s.capture].has<UiTitleBar>()
        && gesture.floatWin != kUiNone && gesture.floatWin < s.count) {
        const float x = in.pointer.x - s.grabX;
        const float y = in.pointer.y - s.grabY;
        const float dx = x - s.box[gesture.floatWin].x;
        const float dy = y - s.box[gesture.floatWin].y;
        if (dx * dx + dy * dy > 16.0f || s.parentOf[gesture.floatWin] == 0) {
            const float w = s.box[gesture.floatWin].w > 40.0f ? s.box[gesture.floatWin].w : 280.0f;
            const float h = s.box[gesture.floatWin].h > 40.0f ? s.box[gesture.floatWin].h : 180.0f;
            if (s.parentOf[gesture.floatWin] != 0) {
                uiReparent(s, gesture.floatWin, 0);
            }
            uiPlace(s, gesture.floatWin, true, x, y, w, h);
        }
        if (hook.move != nullptr) {
            hook.move(s, gesture.floatWin, in.pointer.x, in.pointer.y);
        }
        out.took = 1;
    }
    if (release || !down) {
        if (hook.drop != nullptr) {
            hook.drop(s, gesture.floatWin);
        }
        uiChromeClear(s);
        return out;
    }
    const uint16_t home = gesture.resizeId != kUiNone ? gesture.resizeId : gesture.floatWin;
    if (home == kUiNone) {
        uiChromeClear(s);
    } else {
        uiChromePark(s, home, gesture);
    }
    return out;
}

} // namespace burnhope

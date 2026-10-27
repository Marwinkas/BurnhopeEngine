#include "ui/Geometry.hpp"
#include "ui/Icon.hpp"
#include "ui/UiBin.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Label.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"

namespace burnhope {
namespace {

void onMin(void* user, uint16_t) {
    uiRequestMinimize(*static_cast<UiState*>(user));
}

void onMax(void* user, uint16_t) {
    uiRequestMaximize(*static_cast<UiState*>(user));
}

} // namespace

uint16_t uiFloatWindow(UiState& s, uint16_t parent, float x, float y, float w, float h, bool visible) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/panel", "window_float");
    if (id == kUiNone) {
        return id;
    }
    uiPlace(s, id, true, x, y, w, h);
    uiSetResizable(s, id);
    uiShow(s, id, visible);
    return id;
}

uint16_t uiWindowChrome(UiState& s, uint16_t parent) {
    const UiTemplateId chromeTpl = uiTemplateFind(s, "core/window_chrome");
    const uint16_t cluster = uiSpawnTemplate(s, parent, chromeTpl);
    if (cluster == kUiNone) {
        return cluster;
    }
    const uint16_t minB = uiTemplateSlot(s, chromeTpl, cluster, "min");
    const uint16_t maxB = uiTemplateSlot(s, chromeTpl, cluster, "max");
    const uint16_t closeB = uiTemplateSlot(s, chromeTpl, cluster, "close");
    uiSetIcon(s, minB, UiIcon::Minimize);
    uiSetIcon(s, maxB, UiIcon::Maximize);
    uiSetIcon(s, closeB, UiIcon::Dismiss);
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
    // Blueprint traits for window pieces.
    (void)uiTemplateTrait(s, "titlebar", [](UiState& st, uint16_t id) { st.ent[id].add<UiTitleBar>(); });
    (void)uiTemplateTrait(s, "resizable", [](UiState& st, uint16_t id) { st.ent[id].add<UiResizable>(); });
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
    const float m = uiStyle(s, "window_limits", UiStyleKey::HitSlop);
    const float minW = uiStyle(s, "window_limits", UiStyleKey::MinW);
    const float minH = uiStyle(s, "window_limits", UiStyleKey::MinH);
    const float dragSlop = uiStyle(s, "window_limits", UiStyleKey::DragSlop);
    const float dragMin = uiStyle(s, "window_limits", UiStyleKey::MinDragW);
    auto edgeAt = [m](const UiBox& b, float x, float y) {
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
        uiCapture(s, frame);
        gesture.resizeId = frame;
        gesture.resizeEdge = static_cast<uint8_t>(hoverEdge);
        s.grabX = in.pointer.x;
        s.grabY = in.pointer.y;
        out.took = 1;
    } else if (press && hit != kUiNone && s.ent[hit].has<UiTitleBar>() && s.parentOf[hit] != kUiNone) {
        const uint16_t panel = s.parentOf[hit];
        uiCapture(s, hit);
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
            if (w < minW) {
                if ((gesture.resizeEdge & 1) != 0) {
                    x -= minW - w;
                }
                w = minW;
            }
            if (h < minH) {
                if ((gesture.resizeEdge & 4) != 0) {
                    y -= minH - h;
                }
                h = minH;
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
        if (dx * dx + dy * dy > dragSlop * dragSlop || s.parentOf[gesture.floatWin] == 0) {
            const float w = s.box[gesture.floatWin].w > dragMin ? s.box[gesture.floatWin].w : uiStyle(s, "window_limits", UiStyleKey::Width);
            const float h = s.box[gesture.floatWin].h > dragMin ? s.box[gesture.floatWin].h : uiStyle(s, "window_limits", UiStyleKey::Height);
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

UiChromeGesture* uiChromeFind(UiState& s) {
    const uint16_t id = uiMarked<UiChromeGesture>(s);
    return id < s.count ? s.ent[id].try_get_mut<UiChromeGesture>() : nullptr;
}

void uiChromeClear(UiState& s) {
    const uint16_t id = uiMarked<UiChromeGesture>(s);
    if (id < s.count && s.ent[id].is_alive()) {
        s.ent[id].remove<UiChromeGesture>();
    }
}

void uiChromePark(UiState& s, uint16_t home, const UiChromeGesture& gesture) {
    if (home >= s.count) {
        uiChromeClear(s);
        return;
    }
    const uint16_t prev = uiMarked<UiChromeGesture>(s);
    if (prev < s.count && prev != home && s.ent[prev].is_alive()) {
        s.ent[prev].remove<UiChromeGesture>();
    }
    s.ent[home].set<UiChromeGesture>(gesture);
}

uint16_t uiDragFoldHeader(UiState& s, uint16_t parent, const char* title) {
    const uint16_t header = uiSpawnTemplate(s, parent, "core/row_full", "fold_title");
    if (header == kUiNone) {
        return kUiNone;
    }
    s.ent[header].set<UiTitleBar>({});
    (void)uiLabel(s, header, title);
    (void)uiSpacer(s, header);
    const uint16_t foldBtn = uiSpawnTemplate(s, header, "core/panel", "fold_toggle");
    uiSetIcon(s, foldBtn, UiIcon::Minimize);
    uiHover(s, foldBtn);
    return uiFoldAttach(s, parent, foldBtn);
}

uint16_t uiTitleBar(UiState& s, uint16_t parent, const char* title, UiClickFn onClose, void* user, float height) {
    const uint16_t bar = uiSpawnTemplate(s, parent, "core/row_full", "title_bar");
    if (bar == kUiNone) {
        return kUiNone;
    }
    if (height > 0.0f) {
        uiSetHeight(s, bar, height);
    }
    s.ent[bar].set<UiTitleBar>({});
    (void)uiLabel(s, bar, title);
    (void)uiSpacer(s, bar);
    const uint16_t closeBtn = uiSpawnTemplate(s, bar, "core/panel", "title_mark");
    uiSetIcon(s, closeBtn, UiIcon::Dismiss);
    uiHover(s, closeBtn);
    if (onClose != nullptr) {
        uiBind(s, closeBtn, onClose, user);
    }
    return bar;
}

void uiSetResizable(UiState& s, uint16_t id) {
    if (id < s.count) {
        s.ent[id].set<UiResizable>({});
    }
}

void uiSetTitleBar(UiState& s, uint16_t id) {
    if (id < s.count) {
        s.ent[id].set<UiTitleBar>({});
    }
}

} // namespace burnhope

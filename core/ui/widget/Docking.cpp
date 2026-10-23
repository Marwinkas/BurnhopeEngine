#include "ui/widget/Chrome.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/PrimitiveBatch.hpp"

namespace burnhope {

void uiDockTo(UiState& s, uint8_t edge) {
    const UiDock& dock = uiDock(s);
    if (dock.panel == kUiNone || dock.home == kUiNone || edge == 0) {
        return;
    }
    uiReparent(s, dock.panel, dock.home);
    auto* host = s.ent[dock.home].try_get_mut<UiFlex>();
    auto* panel = s.ent[dock.panel].try_get_mut<UiFlex>();
    if (host == nullptr || panel == nullptr || s.yoga[dock.home] == nullptr || s.yoga[dock.panel] == nullptr) {
        return;
    }
    const bool horizontal = edge == 1 || edge == 2;
    host->direction = horizontal ? UiFlexDir::Row : UiFlexDir::Column;
    panel->position = UiPosition::Static;
    panel->shrink = 0.0f;
    panel->grow = 0.0f;
    if (horizontal) {
        panel->widthMode = UiSize::Px;
        panel->width = 260.0f;
        panel->heightMode = UiSize::Percent;
        panel->height = 100.0f;
    } else {
        panel->widthMode = UiSize::Percent;
        panel->width = 100.0f;
        panel->heightMode = UiSize::Px;
        panel->height = 160.0f;
    }
    if (edge == 1 || edge == 3) {
        uiNodeMoveChild(s, dock.home, dock.panel, 0);
    }
    s.layoutDirty = true;
    s.visualDirty = true;
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
    int hoverEdge = 0;
    uint16_t frame = kUiNone;
    if (s.resizeId == kUiNone) {
        for (uint16_t n = s.count; n > 0; --n) {
            const uint16_t id = static_cast<uint16_t>(n - 1);
            if (!uiIsOverlay(s, id) || s.yoga[id] == nullptr) {
                continue;
            }
            if (!nodeShown(s, id)) {
                continue;
            }
            const bool frameNode = s.ent[id].has<UiResizable>();
            const bool floatingDock = id == uiDock(s).panel && s.parentOf[id] == 0;
            if (!frameNode && !floatingDock) {
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
        frame = s.resizeId;
        hoverEdge = s.resizeEdge;
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
    UiDock& dockChrome = uiDock(s);
    if (dockChrome.edgeOn != 0) {
        dockChrome.edgeOn = 0;
        s.visualDirty = true;
    }
    if (press && hoverEdge != 0 && frame != kUiNone) {
        s.capture = frame;
        s.resizeId = frame;
        s.resizeEdge = static_cast<uint8_t>(hoverEdge);
        s.grabX = in.pointer.x;
        s.grabY = in.pointer.y;
        out.took = 1;
    } else if (press && hit != kUiNone && s.ent[hit].has<UiTitleBar>() && s.parentOf[hit] != kUiNone) {
        const uint16_t panel = s.parentOf[hit];
        s.capture = hit;
        s.floatWin = panel;
        s.grabX = in.pointer.x - s.box[panel].x;
        s.grabY = in.pointer.y - s.box[panel].y;
        out.took = 1;
    }
    if (down && s.resizeId != kUiNone && s.resizeId < s.count && s.capture == s.resizeId) {
        auto* flex = s.ent[s.resizeId].try_get_mut<UiFlex>();
        if (flex != nullptr) {
            float x = flex->posX;
            float y = flex->posY;
            float w = s.box[s.resizeId].w;
            float h = s.box[s.resizeId].h;
            const float dx = in.pointer.x - s.grabX;
            const float dy = in.pointer.y - s.grabY;
            s.grabX = in.pointer.x;
            s.grabY = in.pointer.y;
            if ((s.resizeEdge & 1) != 0) {
                x += dx;
                w -= dx;
            }
            if ((s.resizeEdge & 2) != 0) {
                w += dx;
            }
            if ((s.resizeEdge & 4) != 0) {
                y += dy;
                h -= dy;
            }
            if ((s.resizeEdge & 8) != 0) {
                h += dy;
            }
            if (w < 220.0f) {
                if ((s.resizeEdge & 1) != 0) {
                    x -= 220.0f - w;
                }
                w = 220.0f;
            }
            if (h < 120.0f) {
                if ((s.resizeEdge & 4) != 0) {
                    y -= 120.0f - h;
                }
                h = 120.0f;
            }
            uiPlace(s, s.resizeId, true, x, y, w, h);
        }
        out.took = 1;
    } else if (down && s.capture != kUiNone && s.capture < s.count && s.ent[s.capture].has<UiTitleBar>()
        && s.floatWin != kUiNone && s.floatWin < s.count) {
        const float x = in.pointer.x - s.grabX;
        const float y = in.pointer.y - s.grabY;
        const float dx = x - s.box[s.floatWin].x;
        const float dy = y - s.box[s.floatWin].y;
        if (dx * dx + dy * dy > 16.0f || s.parentOf[s.floatWin] == 0) {
            const float w = s.box[s.floatWin].w > 40.0f ? s.box[s.floatWin].w : 280.0f;
            const float h = s.box[s.floatWin].h > 40.0f ? s.box[s.floatWin].h : 180.0f;
            if (s.parentOf[s.floatWin] != 0) {
                uiReparent(s, s.floatWin, 0);
            }
            uiPlace(s, s.floatWin, true, x, y, w, h);
        }
        UiDock& dock = uiDock(s);
        const uint8_t prevEdge = dock.edge;
        dock.edge = 0;
        dock.hintOn = 0;
        if (s.parentOf[s.floatWin] == 0 && s.floatWin == dock.panel && dock.home != kUiNone && dock.home < s.count
            && hitBox(s.box[dock.home], in.pointer.x, in.pointer.y)) {
            const UiBox& home = s.box[dock.home];
            const float lx = home.w > 1.0f ? (in.pointer.x - home.x) / home.w : 0.5f;
            const float ly = home.h > 1.0f ? (in.pointer.y - home.y) / home.h : 0.5f;
            uint8_t edge = 0;
            if (lx < 0.28f && (dock.edges & 1) != 0) {
                edge = 1;
            } else if (lx > 0.72f && (dock.edges & 2) != 0) {
                edge = 2;
            } else if (ly < 0.28f && (dock.edges & 4) != 0) {
                edge = 3;
            } else if (ly > 0.72f && (dock.edges & 8) != 0) {
                edge = 4;
            }
            dock.edge = edge;
            if (edge != 0) {
                dock.hintOn = 1;
                if (edge == 1) {
                    dock.hint = {home.x, home.y, home.w * 0.34f, home.h};
                } else if (edge == 2) {
                    dock.hint = {home.x + home.w * 0.66f, home.y, home.w * 0.34f, home.h};
                } else if (edge == 3) {
                    dock.hint = {home.x, home.y, home.w, home.h * 0.34f};
                } else {
                    dock.hint = {home.x, home.y + home.h * 0.66f, home.w, home.h * 0.34f};
                }
            }
        }
        if (dock.edge != prevEdge) {
            s.visualDirty = true;
        }
        out.took = 1;
    }
    if (release || !down) {
        if (uiDock(s).edge != 0 && s.floatWin == uiDock(s).panel) {
            uiDockTo(s, uiDock(s).edge);
        }
        s.floatWin = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        UiDock& dockRelease = uiDock(s);
        dockRelease.edge = 0;
        if (dockRelease.hintOn != 0) {
            dockRelease.hintOn = 0;
            s.visualDirty = true;
        }
    }
    return out;
}

void dockOverlayDraw(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    const UiDock& dock = uiDock(s);
    if (dock.hintOn != 0) {
        n = emitRect(dst, n, cap, dock.hint.x, dock.hint.y, dock.hint.w, dock.hint.h, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.35f, 4.0f, nullptr);
    }
    if (dock.edgeOn != 0) {
        n = emitRect(dst, n, cap, dock.edgeLine.x, dock.edgeLine.y, dock.edgeLine.w, dock.edgeLine.h, s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 0.95f, 0.0f, nullptr);
    }
}

void uiInstallDockOverlay(UiState& s) {
    uiOverlayAdd(s, dockOverlayDraw);
}

bool uiDockInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    if (phase == kUiPhase5) {
        const bool plugAte = (cursor & 0x80u) != 0;
        const bool press = (in.pointer.pressed & kPointerLeft) != 0 && !plugAte;
        if (press && id != s.capture) {
            s.resizeId = kUiNone;
            s.resizeEdge = 0;
            s.floatWin = kUiNone;
        }
        if (in.clicks >= 2 && id < s.count) {
            const bool onTitle = s.ent[id].has<UiTitleBar>() || uiAncestorHas<UiTitleBar>(s, id);
            if (onTitle && !s.ent[id].has<UiHoverable>()) {
                uiRequestMaximize(s);
            }
        }
        const bool down = (in.pointer.down & kPointerLeft) != 0;
        const bool release = (in.pointer.released & kPointerLeft) != 0;
        const ChromePointer chrome = uiChromePointer(s, in, id, press, down, release);
        cursor = chrome.cursor;
        if (chrome.took != 0) {
            cursor = static_cast<uint8_t>(cursor | 0x40u);
        }
        return true;
    }
    if (phase == kUiInRelease) {
        s.floatWin = kUiNone;
        s.resizeId = kUiNone;
        s.resizeEdge = 0;
        uiDock(s).edge = 0;
        if (uiDock(s).hintOn != 0) {
            uiDock(s).hintOn = 0;
            s.visualDirty = true;
        }
    }
    return false;
}

} // namespace burnhope

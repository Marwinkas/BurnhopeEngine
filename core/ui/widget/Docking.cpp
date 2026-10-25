#include "ui/Bar.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/Geometry.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/State.hpp"

namespace burnhope {

void uiDockTo(UiState& s, uint8_t edge) {
    const UiDock* dock = uiDockFind(s);
    if (dock == nullptr || dock->panel == kUiNone || dock->home == kUiNone || edge == 0) {
        return;
    }
    uiReparent(s, dock->panel, dock->home);
    auto* host = s.ent[dock->home].try_get_mut<UiFlex>();
    auto* panel = s.ent[dock->panel].try_get_mut<UiFlex>();
    if (host == nullptr || panel == nullptr || s.yoga[dock->home] == nullptr || s.yoga[dock->panel] == nullptr) {
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
        uiNodeMoveChild(s, dock->home, dock->panel, 0);
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

namespace {

bool dockChromeFrame(const UiState& s, uint16_t id) {
    const UiDock* dock = uiDockFind(s);
    return dock != nullptr && id == dock->panel && s.parentOf[id] == 0;
}

void dockChromeMove(UiState& s, uint16_t panel, float px, float py) {
    UiDock* dock = uiDockFind(s);
    if (dock == nullptr) {
        return;
    }
    const uint8_t prevEdge = dock->edge;
    dock->edge = 0;
    dock->hintOn = 0;
    if (panel < s.count && s.parentOf[panel] == 0 && panel == dock->panel && dock->home != kUiNone && dock->home < s.count
        && hitBox(s.box[dock->home], px, py)) {
        const UiBox& home = s.box[dock->home];
        const float lx = home.w > 1.0f ? (px - home.x) / home.w : 0.5f;
        const float ly = home.h > 1.0f ? (py - home.y) / home.h : 0.5f;
        uint8_t edge = 0;
        if (lx < 0.28f && (dock->edges & 1) != 0) {
            edge = 1;
        } else if (lx > 0.72f && (dock->edges & 2) != 0) {
            edge = 2;
        } else if (ly < 0.28f && (dock->edges & 4) != 0) {
            edge = 3;
        } else if (ly > 0.72f && (dock->edges & 8) != 0) {
            edge = 4;
        }
        dock->edge = edge;
        if (edge != 0) {
            dock->hintOn = 1;
            if (edge == 1) {
                dock->hint = {home.x, home.y, home.w * 0.34f, home.h};
            } else if (edge == 2) {
                dock->hint = {home.x + home.w * 0.66f, home.y, home.w * 0.34f, home.h};
            } else if (edge == 3) {
                dock->hint = {home.x, home.y, home.w, home.h * 0.34f};
            } else {
                dock->hint = {home.x, home.y + home.h * 0.66f, home.w, home.h * 0.34f};
            }
        }
    }
    if (dock->edge != prevEdge) {
        s.visualDirty = true;
    }
}

void dockChromeDrop(UiState& s, uint16_t panel) {
    UiDock* dock = uiDockFind(s);
    if (dock == nullptr) {
        return;
    }
    if (panel != kUiNone && dock->edge != 0 && panel == dock->panel) {
        uiDockTo(s, dock->edge);
        dock = uiDockFind(s);
        if (dock == nullptr) {
            return;
        }
    }
    dock->edge = 0;
    if (dock->hintOn != 0) {
        dock->hintOn = 0;
        s.visualDirty = true;
    }
}

} // namespace

void dockPlace(UiState& s) {
    UiNodeBars bars{};
    const UiDock* dock = uiDockFind(s);
    auto push = [&](float x, float y, float w, float h, float a, float radius) {
        UiBar bar{};
        bar.x = x;
        bar.y = y;
        bar.w = w;
        bar.h = h;
        bar.radius = radius;
        bar.r = s.look.accent[0][0];
        bar.g = s.look.accent[0][1];
        bar.b = s.look.accent[0][2];
        bar.a = a;
        bar.on = 1;
        uiNodeBarPush(s, bars, bar);
    };
    if (dock != nullptr && dock->hintOn != 0) {
        push(dock->hint.x, dock->hint.y, dock->hint.w, dock->hint.h, 0.35f, 4.0f);
    }
    if (dock != nullptr && dock->edgeOn != 0) {
        push(dock->edgeLine.x, dock->edgeLine.y, dock->edgeLine.w, dock->edgeLine.h, 0.95f, 0.0f);
    }
    UiStamps stamps{};
    stamps.bar = bars.bar;
    stamps.n = bars.n;
    s.world.ensure<UiStamps>() = stamps;
}

void uiInstallDockOverlay(UiState& s) {
    uiChromeSetDock(s, dockChromeFrame, dockChromeMove, dockChromeDrop);
    uiPlaceAdd(s, dockPlace);
}

bool uiDockInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note) {
    if (phase == kUiPhase5) {
        const bool plugAte = note.plugAte != 0;
        const bool press = (in.pointer.pressed & kPointerLeft) != 0 && !plugAte;
        if (UiDock* dock = uiDockFind(s); dock != nullptr && dock->edgeOn != 0) {
            dock->edgeOn = 0;
            s.visualDirty = true;
        }
        if (press && id != s.capture) {
            uiChromeClear(s);
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
        note.cursor = chrome.cursor;
        note.took = chrome.took;
        return true;
    }
    if (phase == kUiInRelease) {
        uiChromeClear(s);
        if (UiDock* dock = uiDockFind(s)) {
            dock->edge = 0;
            if (dock->hintOn != 0) {
                dock->hintOn = 0;
                s.visualDirty = true;
            }
        }
    }
    return false;
}

UiDock* uiDockFind(UiState& s) {
    const uint16_t id = uiMarked<UiDock>(s);
    return id < s.count ? s.ent[id].try_get_mut<UiDock>() : nullptr;
}

const UiDock* uiDockFind(const UiState& s) {
    const uint16_t id = uiMarked<UiDock>(s);
    return id < s.count ? s.ent[id].try_get<UiDock>() : nullptr;
}

void uiDockBind(UiState& s, uint16_t home, uint16_t panel, uint8_t edges) {
    if (home >= s.count) {
        return;
    }
    const uint16_t prev = uiMarked<UiDock>(s);
    UiDock dock{};
    if (prev == home) {
        if (const UiDock* live = s.ent[home].try_get<UiDock>()) {
            dock = *live;
        }
    } else if (prev < s.count) {
        s.ent[prev].remove<UiDock>();
    }
    dock.home = home;
    dock.panel = panel;
    dock.edges = edges;
    s.ent[home].set<UiDock>(dock);
}

} // namespace burnhope

#include "ui/widget/DragDrop.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/VirtualGrid.hpp"

namespace burnhope {
namespace {

void clearMark(UiState& s, UiDragDrop& drag) {
    if (drag.mark < s.count) {
        if (auto* paint = s.ent[drag.mark].try_get_mut<UiPaint>()) {
            paint->borderW = 0.0f;
        }
    }
    drag.mark = kUiNone;
}

void showMark(UiState& s, UiDragDrop& drag, uint16_t row) {
    if (row == drag.mark) {
        return;
    }
    clearMark(s, drag);
    if (row >= s.count) {
        return;
    }
    if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
        paint->borderW = 1.0f;
        paint->br = s.look.accent[0][0];
        paint->bg = s.look.accent[0][1];
        paint->bb = s.look.accent[0][2];
    }
    drag.mark = row;
}

uint16_t ensureGhost(UiState& s, UiDragDrop& drag) {
    if (drag.ghost < s.count && s.ent[drag.ghost].is_alive()) {
        return drag.ghost;
    }
    UiFlex box = px(s.metrics.ghostW, s.metrics.ghostH);
    box.position = UiPosition::Absolute;
    UiPaint ink = paintToken(s.look, UiToken::Menu, s.look.radiusSm);
    ink.a = s.metrics.ghostA;
    const uint16_t id = spawn(s, kUiNone, box, ink);
    if (id == kUiNone) {
        return kUiNone;
    }
    uiText(s, id, "0");
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    s.ent[id].add<UiPopup>();
    s.ent[id].add<UiNoHit>();
    uiShow(s, id, false);
    drag.ghost = id;
    return id;
}

void endDrag(UiState& s, UiDragDrop& drag, bool drop) {
    uint16_t dstHost = kUiNone;
    uint32_t dstIndex = kUiDragNone;
    if (drop) {
        for (uint8_t i = 0; i < drag.hosts; ++i) {
            const uint16_t host = drag.host[i];
            if (host >= s.count || (s.hovered != host && !uiWithin(s, s.hovered, host))) {
                continue;
            }
            const bool virt = s.ent[host].try_get<UiVirtualGrid>() != nullptr || s.ent[host].try_get<UiTreeView>() != nullptr;
            uint32_t index = 0;
            if (virt) {
                (void)uiVirtualHitRow(s, host, index);
            }
            if (virt && index == kUiDragNone) {
                continue;
            }
            if (drag.canDrop != nullptr && drag.canDrop(drag.user, host, index) == 0) {
                continue;
            }
            dstHost = host;
            dstIndex = index;
            break;
        }
    }
    clearMark(s, drag);
    if (drag.ghost < s.count) {
        uiShow(s, drag.ghost, false);
    }
    const uint16_t srcHost = drag.srcHost;
    const uint32_t srcIndex = drag.srcIndex;
    drag.armed = 0;
    drag.live = 0;
    drag.srcHost = kUiNone;
    drag.srcIndex = kUiDragNone;
    if (drop && dstHost != kUiNone && drag.onDrop != nullptr) {
        drag.onDrop(drag.user, srcHost, srcIndex, dstHost, dstIndex);
    }
}

} // namespace

void uiDragDropBind(UiState& s, const UiDragDropConfig& cfg) {
    UiDragDrop& drag = s.world.ensure<UiDragDrop>();
    const uint16_t ghost = drag.ghost;
    const uint16_t mark = drag.mark;
    drag = {};
    drag.ghost = ghost;
    drag.mark = mark;
    const uint16_t hosts[3] = {cfg.host0, cfg.host1, cfg.host2};
    for (uint8_t i = 0; i < 3 && drag.hosts < kUiDragHostCap; ++i) {
        if (hosts[i] != kUiNone) {
            drag.host[drag.hosts++] = hosts[i];
        }
    }
    drag.canDrag = cfg.canDrag;
    drag.canDrop = cfg.canDrop;
    drag.onDrop = cfg.onDrop;
    drag.badge = cfg.badge;
    drag.user = cfg.user;
}

bool uiDragDropInput(UiState& s, const InputFrame& in, uint16_t, uint8_t phase, uint8_t&) {
    if (phase != kUiInSync && phase != kUiInPress) {
        return false;
    }
    UiDragDrop* drag = s.world.try_get_mut<UiDragDrop>();
    if (drag == nullptr || drag->hosts == 0) {
        return false;
    }
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    if (release && drag->live != 0) {
        endDrag(s, *drag, true);
        return false;
    }
    if (!down && drag->live != 0) {
        endDrag(s, *drag, true);
        return false;
    }
    if (!down) {
        drag->armed = 0;
    }
    if (press) {
        drag->armed = 0;
        drag->x = in.pointer.x;
        drag->y = in.pointer.y;
        for (uint8_t i = 0; i < drag->hosts; ++i) {
            const uint16_t host = drag->host[i];
            uint32_t index = kUiDragNone;
            if (uiVirtualHitRow(s, host, index) == kUiNone || index == kUiDragNone) {
                continue;
            }
            if (drag->canDrag != nullptr && drag->canDrag(drag->user, host, index) == 0) {
                continue;
            }
            drag->armed = 1;
            drag->srcHost = host;
            drag->srcIndex = index;
            break;
        }
    }
    if (down && drag->armed != 0 && drag->live == 0) {
        const float dx = in.pointer.x - drag->x;
        const float dy = in.pointer.y - drag->y;
        const float slop = s.metrics.dragSlop;
        if (dx * dx + dy * dy > slop * slop) {
            drag->live = 1;
        }
    }
    if (drag->live == 0) {
        return false;
    }
    const uint16_t ghost = ensureGhost(s, *drag);
    if (ghost < s.count) {
        char badge[32]{};
        badge[0] = '0';
        badge[1] = '\0';
        if (drag->badge != nullptr) {
            drag->badge(drag->user, badge, 32);
        }
        uiText(s, ghost, badge);
        uiShow(s, ghost, true);
        uiPlace(s, ghost, true, in.pointer.x + 12.0f, in.pointer.y + 12.0f, s.metrics.ghostW, s.metrics.ghostH);
    }
    uint16_t row = kUiNone;
    for (uint8_t i = 0; i < drag->hosts; ++i) {
        const uint16_t host = drag->host[i];
        if (host >= s.count || (s.hovered != host && !uiWithin(s, s.hovered, host))) {
            continue;
        }
        const bool virt = s.ent[host].try_get<UiVirtualGrid>() != nullptr || s.ent[host].try_get<UiTreeView>() != nullptr;
        uint32_t index = 0;
        const uint16_t hit = virt ? uiVirtualHitRow(s, host, index) : host;
        if (virt && (hit == kUiNone || index == kUiDragNone)) {
            continue;
        }
        if (drag->canDrop != nullptr && drag->canDrop(drag->user, host, index) == 0) {
            continue;
        }
        row = hit;
        break;
    }
    showMark(s, *drag, row);
    return false;
}

} // namespace burnhope

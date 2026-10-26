#include "ui/widget/DragDrop.hpp"

#include "ui/Geometry.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/State.hpp"
#include "ui/Template.hpp"
#include "ui/StyleSheet.hpp"

#include <algorithm>

namespace burnhope {
namespace {

uint16_t nextHost(const UiState& s, uint16_t start) {
    uint16_t hosts[kUiCap];
    const uint16_t count = uiCollect<UiDragHost>(s, hosts, kUiCap);
    for (uint16_t h = 0; h < count; ++h) {
        if (hosts[h] >= start) {
            return hosts[h];
        }
    }
    return kUiNone;
}

UiDragDrop* dragSession(UiState& s) {
    const uint16_t id = uiMarked<UiDragDrop>(s);
    return id < s.count ? s.ent[id].try_get_mut<UiDragDrop>() : nullptr;
}

void hideMark(UiState& s, UiDragDrop& drag) {
    if (drag.mark < s.count) {
        uiShow(s, drag.mark, false);
    }
}

uint16_t ensureMark(UiState& s, UiDragDrop& drag) {
    if (drag.mark < s.count && s.ent[drag.mark].is_alive()) {
        return drag.mark;
    }
    UiFlex box = px(1.0f, 1.0f);
    box.position = UiPosition::Absolute;
    UiPaint ink = paintToken(s.look, UiToken::Accent, s.look.radiusSm);
    ink.a = 0.0f;
    ink.borderW = 1.0f;
    const uint16_t id = spawn(s, kUiNone, box, ink);
    if (id == kUiNone) {
        return kUiNone;
    }
    s.ent[id].add<UiPopup>();
    s.ent[id].add<UiNoHit>();
    uiShow(s, id, false);
    drag.mark = id;
    return id;
}

void showMark(UiState& s, UiDragDrop& drag, uint16_t row) {
    if (row >= s.count) {
        hideMark(s, drag);
        return;
    }
    const uint16_t mark = ensureMark(s, drag);
    if (mark >= s.count) {
        return;
    }
    const UiBox& box = s.box[row];
    uiPlace(s, mark, true, box.x, box.y, box.w > 1.0f ? box.w : 1.0f, box.h > 1.0f ? box.h : 1.0f);
    uiShow(s, mark, true);
    uiRequestTop(s, true);
}

uint16_t ensureGhost(UiState& s, UiDragDrop& drag) {
    if (drag.ghost < s.count && s.ent[drag.ghost].is_alive()) {
        return drag.ghost;
    }
    const uint16_t id = uiSpawnTemplate(s, kUiNone, "core/ghost");
    if (id == kUiNone) {
        return kUiNone;
    }
    uiPlace(s, id, true, 0.0f, 0.0f, 0.0f, 0.0f);
    uiSetIcon(s, id, UiIcon::Document);
    uiText(s, id, "0");
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
        for (uint16_t host = nextHost(s, 0); host != kUiNone; host = nextHost(s, static_cast<uint16_t>(host + 1))) {
            if (host >= s.count || (s.hovered != host && !uiWithin(s, s.hovered, host))) {
                continue;
            }
            const bool virt = s.ent[host].has<UiVirtualPool>();
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
        if (dstHost == kUiNone) {
            for (uint16_t host = nextHost(s, 0); host != kUiNone; host = nextHost(s, static_cast<uint16_t>(host + 1))) {
                if (host >= s.count) {
                    continue;
                }
                const bool virt = s.ent[host].has<UiVirtualPool>();
                if (virt || !hitBox(s.box[host], drag.dropX, drag.dropY)) {
                    continue;
                }
                if (drag.canDrop != nullptr && drag.canDrop(drag.user, host, 0) == 0) {
                    continue;
                }
                dstHost = host;
                dstIndex = 0;
                break;
            }
        }
    }
    hideMark(s, drag);
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
    UiDragDrop born{};
    if (UiDragDrop* live = dragSession(s)) {
        born.ghost = live->ghost;
        born.mark = live->mark;
    }
    const uint16_t prev = uiMarked<UiDragDrop>(s);
    if (prev < s.count) {
        s.ent[prev].remove<UiDragDrop>();
    }
    uint16_t oldHosts[kUiCap];
    const uint16_t oldCount = uiCollect<UiDragHost>(s, oldHosts, kUiCap);
    for (uint16_t h = 0; h < oldCount; ++h) {
        s.ent[oldHosts[h]].remove<UiDragHost>();
    }
    born.canDrag = cfg.canDrag;
    born.canDrop = cfg.canDrop;
    born.onDrop = cfg.onDrop;
    born.badge = cfg.badge;
    born.badgeIcon = cfg.badgeIcon;
    born.user = cfg.user;
    uint16_t home = kUiNone;
    for (uint16_t host : cfg.hosts) {
        if (host == kUiNone || host >= s.count) {
            continue;
        }
        s.ent[host].add<UiDragHost>();
        if (home == kUiNone) {
            home = host;
        }
    }
    if (home != kUiNone) {
        s.ent[home].set<UiDragDrop>(born);
    }
}

void uiDragAddHost(UiState& s, uint16_t host) {
    if (host >= s.count) {
        return;
    }
    s.ent[host].add<UiDragHost>();
}

bool uiDragDropInput(UiState& s, const InputFrame& in, uint16_t, uint8_t phase, UiPhaseNote&) {
    if (phase != kUiInSync && phase != kUiInPress && phase != kUiInEscape) {
        return false;
    }
    UiDragDrop* drag = dragSession(s);
    if (drag == nullptr) {
        return false;
    }
    if (phase == kUiInEscape) {
        if (drag->live != 0 || drag->armed != 0) {
            endDrag(s, *drag, false);
        }
        return false;
    }
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    drag->dropX = in.pointer.x;
    drag->dropY = in.pointer.y;
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
        for (uint16_t host = nextHost(s, 0); host != kUiNone; host = nextHost(s, static_cast<uint16_t>(host + 1))) {
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
        const float slop = uiStyle(s, "drag_ghost", UiStyleKey::DragSlop);
        if (dx * dx + dy * dy > slop * slop) {
            drag->live = 1;
        }
    }
    if (drag->live == 0) {
        return false;
    }
    const uint16_t ghost = ensureGhost(s, *drag);
    if (ghost < s.count) {
        char badge[64]{};
        badge[0] = '0';
        badge[1] = '\0';
        if (drag->badge != nullptr) {
            drag->badge(drag->user, badge, 64);
        }
        uiText(s, ghost, badge);
        if (auto* paint = s.ent[ghost].try_get_mut<UiPaint>()) {
            paint->icon = drag->badgeIcon != nullptr ? drag->badgeIcon(drag->user) : static_cast<uint8_t>(UiIcon::Document);
        }
        uiShow(s, ghost, true);
        uiRequestTop(s, true);
        uint8_t letters = 0;
        while (badge[letters] != '\0' && letters < 64) {
            ++letters;
        }
        const float iconWidth = uiStyle(s, "drag_ghost", UiStyleKey::IconLeft) + uiStyle(s, "drag_ghost", UiStyleKey::Icon);
        float gw = static_cast<float>(letters) * uiStyle(s, "drag_ghost", UiStyleKey::GlyphW) + iconWidth + 2.0f * uiStyle(s, "drag_ghost", UiStyleKey::Gap);
        gw = std::clamp(gw, uiStyle(s, "drag_ghost", UiStyleKey::MinW), uiStyle(s, "drag_ghost", UiStyleKey::MaxW));
        const float cursorOffset = uiStyle(s, "drag_ghost", UiStyleKey::Offset);
        uiPlace(s, ghost, true, in.pointer.x + cursorOffset, in.pointer.y + cursorOffset, gw, uiStyle(s, "drag_ghost", UiStyleKey::Height));
    }
    uint16_t row = kUiNone;
    for (uint16_t host = nextHost(s, 0); host != kUiNone; host = nextHost(s, static_cast<uint16_t>(host + 1))) {
        if (host >= s.count || (s.hovered != host && !uiWithin(s, s.hovered, host))) {
            continue;
        }
        const bool virt = s.ent[host].has<UiVirtualPool>();
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

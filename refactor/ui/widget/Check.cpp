#include "ui/widget/Check.hpp"

#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/widget/Mark.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/Text.hpp"

#include <bit>
#include <cstdint>

namespace burnhope {
namespace {

void onCheck(void* user, uint16_t id) {
    uiCheckPress(*static_cast<UiState*>(user), id);
}

void markCheck(UiState& s, uint16_t id, const UiCheck& check) {
    s.ent[id].set<UiMark>({
        .shape = check.group != 0 ? uint8_t{1} : uint8_t{0},
        .on = check.on,
    });
}

void hostText(UiState& s, uint16_t id) {
    if (s.ent[id].try_get<UiText>() != nullptr) {
        return;
    }
    UiText host{};
    host.align = UiTextAlign::Left;
    host.ellipsis = UiTextEllipsis::End;
    host.valign = UiTextValign::Center;
    s.ent[id].set<UiText>(host);
}

// Check rows are transparent: only the box and the label draw; hover never fills the row.
void clearRow(UiState& s, uint16_t id) {
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paintGhost(*paint);
        const UiToken ink = uiStyleBg(s, "checkbox");
        paintFillToken(s.look, *paint, ink);
        paintHoverToken(s.look, *paint, ink, uiStyle(s, "checkbox", UiStyleKey::HoverAlpha));
    }
}

void adornCheck(UiState& s, uint16_t id) {
    hostText(s, id);
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        const float padL = uiStyle(s, "checkbox", UiStyleKey::PadL);
        if (flex->padL < padL) {
            flex->padL = padL;
        }
    }
}

bool drawCheck(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    const UiMark* mark = s.ent[id].try_get<UiMark>();
    if (mark == nullptr) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    using K = UiStyleKey;
    const float side = uiStyle(s, "check_box", K::Width);
    const bool hot = s.hovered == id;
    const UiRgb edge = uiStyleTint(s, "check_box", hot ? UiStyleTone::Hover : UiStyleTone::Mark);
    const float x = box.w < uiStyle(s, "check_box", K::CompactW) ? box.x + (box.w - side) * 0.5f : box.x + uiStyle(s, "check_box", K::IconLeft);
    const float y = box.y + (box.h - side) * 0.5f;
    if (mark->shape != 0) {
        n = emitMark(dst, n, cap, x, y, side, side, edge.r, edge.g, edge.b, 1.0f, side * 0.5f, uiStyle(s, "check_box", K::BorderRadio), kUiFlagStroke, clip);
        if (mark->on != 0) {
            const float dot = uiStyle(s, "check_box", K::Inner);
            const UiRgb ink = uiStyleTint(s, "check_box", UiStyleTone::Text);
            n = emitRect(dst, n, cap, x + (side - dot) * 0.5f, y + (side - dot) * 0.5f, dot, dot, ink.r, ink.g, ink.b, 1.0f, dot * 0.5f, clip);
        }
        return true;
    }
    n = emitMark(dst, n, cap, x, y, side, side, edge.r, edge.g, edge.b, 1.0f, uiStyle(s, "check_box", K::Radius), uiStyle(s, "check_box", K::Border), kUiFlagStroke, clip);
    if (mark->on != 0) {
        const float inset = uiStyle(s, "check_box", K::MarkInset);
        const UiRgb tick = uiStyleTint(s, "check_box", UiStyleTone::MarkActive);
        n = emitIcon(dst, n, cap, x + inset, y + inset, side - 2.0f * inset, tick.r, tick.g, tick.b, 1.0f, static_cast<uint32_t>(UiIcon::Check), 0.0f, clip);
    }
    return true;
}

void finishCheck(UiState& s, uint16_t id, const BinTail* spec, const char*, const char*) {
    uint8_t on = 0;
    uint8_t kind = 0;
    uint8_t group = 0;
    if (spec != nullptr) {
        constexpr uint8_t kOn = 1 << 6;
        on = (spec->raw[1] & kOn) != 0 ? 1 : 0;
        kind = spec->raw[2];
        group = spec->raw[3];
    }
    clearRow(s, id);
    uiHover(s, id);
    uiSetFocusable(s, id);
    uiBindCheck(s, id, kind, group, on != 0);
}

} // namespace

uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind, uint8_t group, bool on) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/checkbox");
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text != nullptr ? text : "");
    uiBindCheck(s, id, kind, group, on);
    return id;
}

void uiCheckAssign(UiState& s, uint16_t id, uint8_t on) {
    if (id >= s.count) {
        return;
    }
    auto* check = s.ent[id].try_get_mut<UiCheck>();
    if (check == nullptr) {
        return;
    }
    check->on = on;
    markCheck(s, id, *check);
    s.visualDirty = true;
}

static bool underParent(const UiState& s, uint16_t node, uint16_t parent) {
    uint16_t p = node;
    for (int hop = 0; hop < 32 && p != kUiNone && p < s.count; ++hop) {
        if (p == parent) {
            return true;
        }
        const uint16_t next = s.parentOf[p];
        if (next == p) {
            break;
        }
        p = next;
    }
    return false;
}

void uiCheckPress(UiState& s, uint16_t id) {
    UiCheck* self = id < s.count ? s.ent[id].try_get_mut<UiCheck>() : nullptr;
    if (self == nullptr) {
        return;
    }
    if (self->group != 0) {
        const uint8_t group = self->group;
        const uint16_t parent = s.parentOf[id];
        uint16_t checks[kUiCap];
        const uint16_t checkCount = uiCollect<UiCheck>(s, checks, kUiCap);
        for (uint16_t c = 0; c < checkCount; ++c) {
            const uint16_t other = checks[c];
            if (other == id) {
                continue;
            }
            const bool same = parent == kUiNone ? s.parentOf[other] == kUiNone : underParent(s, other, parent);
            if (!same) {
                continue;
            }
            if (auto* check = s.ent[other].try_get_mut<UiCheck>(); check != nullptr && check->group == group) {
                check->on = 0;
                markCheck(s, other, *check);
            }
        }
        self = s.ent[id].try_get_mut<UiCheck>();
        if (self != nullptr) {
            self->on = 1;
            markCheck(s, id, *self);
        }
    } else {
        self->on = self->on == 0 ? 1 : 0;
        markCheck(s, id, *self);
    }
    s.visualDirty = true;
}

void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on) {
    if (id >= s.count) {
        return;
    }
    s.ent[id].set<UiCheck>({on ? uint8_t{1} : uint8_t{0}, kind, group});
    if (const UiCheck* check = s.ent[id].try_get<UiCheck>()) {
        markCheck(s, id, *check);
    }
    uiBind(s, id, onCheck, &s);
    s.visualDirty = true;
}

int uiCheckOn(const UiState& s, uint16_t id) {
    if (id == kUiNone || id >= s.count) {
        return -1;
    }
    const UiCheck* check = s.ent[id].try_get<UiCheck>();
    if (check == nullptr) {
        return -1;
    }
    return check->on != 0 ? 1 : 0;
}

int uiCheckOn(const UiState& s, const char* name) {
    if (name == nullptr) {
        return -1;
    }
    return uiCheckOn(s, uiFindName(s, name));
}

void uiCheckBin(UiState& s) {
    uiBinRole(s, kBinSlotCheck, "check", static_cast<uint8_t>(UiToken::Check), 0);
    uiRegisterSpawnHook(s, kBinSlotCheck, adornCheck);
    uiRegisterRoleFinish(s, kBinSlotCheck, finishCheck);
    uiDrawDecorAdd(s, drawCheck);
}

} // namespace burnhope

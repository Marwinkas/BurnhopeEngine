#include "ui/widget/Button.hpp"

#include <bit>
#include "ui/widget/Image.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/Text.hpp"

namespace burnhope {

void activate(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint != nullptr && paint->disabled) {
        return;
    }
    uiTrace(s, "activate", id);
    if (const UiAction* act = s.ent[id].try_get<UiAction>(); act != nullptr && act->fn != nullptr) {
        act->fn(act->user, id);
    } else if (s.onClick != nullptr) {
        s.onClick(s.clickUser, id);
    }
}

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    flex.align = UiAlign::Center;
    flex.justify = UiJustify::Center;
    if (cfg.fillHeight) {
        flex.heightMode = UiSize::Percent;
        flex.height = 100.0f;
    } else {
        flex.heightMode = UiSize::Px;
        flex.height = cfg.height > 0.0f ? cfg.height : s.metrics.buttonH;
    }
    const float padX = s.metrics.buttonPadX;
    if (cfg.fillWidth) {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
        flex.grow = 1.0f;
        flex.padL = padX;
        flex.padR = padX;
    } else if (cfg.width > 0.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = cfg.width;
    } else {
        flex.padL = padX;
        flex.padR = padX;
    }
    const uint16_t id = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Button, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, cfg.label != nullptr ? cfg.label : "");
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paintRaised(*paint);
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    if (cfg.iconSlot < kPhotoSlots) {
        const uint16_t iconId = uiIcon(s, id, cfg.iconSlot, 16.0f, 16.0f);
        if (iconId != kUiNone) {
            s.ent[iconId].add<UiNoHit>();
        }
    }
    if (cfg.onClick != nullptr) {
        uiBind(s, id, cfg.onClick, cfg.user);
    }
    return id;
}

uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind, uint8_t group, bool on) {
    UiFlex flex{};
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.checkH;
    flex.padL = 36.0f;
    flex.padR = 8.0f;
    const uint16_t id = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Check, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text != nullptr ? text : "");
    uiTextStyle(s, id, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    uiBindCheck(s, id, kind, group, on);
    return id;
}

static void onCheck(void* user, uint16_t id) {
    uiCheckPress(*static_cast<UiState*>(user), id);
}

void uiCheckPress(UiState& s, uint16_t id) {
    UiCheck* self = id < s.count ? s.ent[id].try_get_mut<UiCheck>() : nullptr;
    if (self == nullptr) {
        return;
    }
    if (self->group != 0) {
        const uint8_t group = self->group;
        for (uint16_t other = 0; other < s.count; ++other) {
            if (other == id) {
                continue;
            }
            if (auto* check = s.ent[other].try_get_mut<UiCheck>(); check != nullptr && check->group == group) {
                check->on = 0;
            }
        }
        self = s.ent[id].try_get_mut<UiCheck>();
        if (self != nullptr) {
            self->on = 1;
        }
    } else {
        self->on = self->on == 0 ? 1 : 0;
    }
    s.visualDirty = true;
}

void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on) {
    if (id >= s.count) {
        return;
    }
    s.ent[id].set<UiCheck>({on ? uint8_t{1} : uint8_t{0}, kind, group});
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

bool uiCheckDecor(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (id >= s.count) {
        return false;
    }
    const UiCheck* check = s.ent[id].try_get<UiCheck>();
    if (check == nullptr) {
        return false;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    const float side = 22.0f;
    const float x = box.w < 40.0f ? box.x + (box.w - side) * 0.5f : box.x + 8.0f;
    const float y = box.y + (box.h - side) * 0.5f;
    if (check->group != 0) {
        n = emitMark(dst, n, cap, x, y, side, side, s.look.muted[0], s.look.muted[1], s.look.muted[2], 1.0f, side * 0.5f, 1.5f, kUiFlagStroke, clip);
        if (check->on != 0) {
            const float dot = 10.0f;
            n = emitRect(dst, n, cap, x + (side - dot) * 0.5f, y + (side - dot) * 0.5f, dot, dot,
                s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, dot * 0.5f, clip);
        }
        return false;
    }
    n = emitMark(dst, n, cap, x, y, side, side, s.look.muted[0], s.look.muted[1], s.look.muted[2], 1.0f, 2.0f, 1.0f, kUiFlagStroke, clip);
    if (check->on != 0 && n < cap) {
        UiPrimitive& mark = dst[n++];
        mark = {};
        mark.posX = x + 1.0f;
        mark.posY = y + 1.0f;
        mark.sizeX = side - 2.0f;
        mark.sizeY = side - 2.0f;
        mark.color[0] = s.look.text[0];
        mark.color[1] = s.look.text[1];
        mark.color[2] = s.look.text[2];
        mark.color[3] = 1.0f;
        mark.texId = 9;
        mark.flags = kUiFlagIcon;
        if (clip != nullptr) {
            mark.flags |= kUiFlagClip;
            mark.extra[1] = clip->x;
            mark.extra[2] = clip->y;
            mark.extra[3] = clip->w;
            mark.pad0 = std::bit_cast<uint32_t>(clip->h);
        }
    }
    return false;
}

void uiSetDisabled(UiState& s, uint16_t id, bool disabled) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr || paint->disabled == disabled) {
        return;
    }
    paint->disabled = disabled;
    uiNotifyVisualDirty(s);
}

int uiCheckOn(const UiState& s, const char* name) {
    if (name == nullptr) {
        return -1;
    }
    return uiCheckOn(s, uiFindName(s, name));
}

} // namespace burnhope

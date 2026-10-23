#include "ui/Icon.hpp"
#include "ui/widget/Button.hpp"

#include <bit>
#include "ui/widget/Image.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"

namespace burnhope {

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
    const uint16_t id = uiPanel(s, parent, flex, paintButton(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, cfg.label != nullptr ? cfg.label : "");
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
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
        mark.color[0] = s.look.accent[0][0];
        mark.color[1] = s.look.accent[0][1];
        mark.color[2] = s.look.accent[0][2];
        mark.color[3] = 1.0f;
        mark.texId = static_cast<uint32_t>(UiIcon::Check);
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

uint16_t uiIconBtn(UiState& s, uint16_t parent, uint8_t icon, float side, uint16_t* glyph) {
    const uint16_t id = uiButton(s, parent, {.label = "", .width = side, .height = side});
    if (id >= s.count) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->pad = 0.0f;
        flex->padL = 0.0f;
        flex->padR = 0.0f;
        flex->padT = 0.0f;
        flex->padB = 0.0f;
        flex->align = UiAlign::Center;
        flex->justify = UiJustify::Center;
    }
    s.ent[id].add<UiIconChrome>();
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paintGhost(*paint);
        paint->icon = icon;
        paint->plate = 1;
        paint->r = s.look.text[0];
        paint->g = s.look.text[1];
        paint->b = s.look.text[2];
    }
    if (glyph != nullptr) {
        *glyph = id;
    }
    return id;
}

uint16_t uiSlotBtn(UiState& s, uint16_t parent, uint8_t slot, float side, uint16_t* icon) {
    const uint16_t id = uiButton(s, parent, {.label = "", .width = side, .height = side, .iconSlot = slot});
    if (id >= s.count) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->pad = 0.0f;
        flex->padL = 0.0f;
        flex->padR = 0.0f;
        flex->padT = 0.0f;
        flex->padB = 0.0f;
        flex->align = UiAlign::Center;
        flex->justify = UiJustify::Center;
    }
    s.ent[id].add<UiIconChrome>();
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paintGhost(*paint);
    }
    const uint16_t child = uiButtonIconChild(s, id);
    if (child < s.count) {
        if (auto* flex = s.ent[child].try_get_mut<UiFlex>()) {
            flex->width = side;
            flex->height = side;
        }
        if (auto* glyph = s.ent[child].try_get_mut<UiPaint>()) {
            glyph->plate = 1;
        }
    }
    if (icon != nullptr) {
        *icon = child;
    }
    return id;
}

void uiButtonTintIcon(UiState& s, uint16_t icon, bool on) {
    auto* paint = icon < s.count ? s.ent[icon].try_get_mut<UiPaint>() : nullptr;
    if (paint == nullptr) {
        return;
    }
    paint->token = on ? UiToken::Accent : UiToken::Text;
    paint->picked = on ? 1 : 0;
    paint->plate = 1;
    const float* rgb = on ? s.look.accent[0] : s.look.text;
    paint->r = rgb[0];
    paint->g = rgb[1];
    paint->b = rgb[2];
    paint->a = 0.0f;
    uiNotifyVisualDirty(s);
}

uint16_t uiButtonIconChild(const UiState& s, uint16_t id) {
    const uint32_t n = uiNodeChildCount(s, id);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t child = uiNodeChild(s, id, i);
        if (child < s.count && s.ent[child].has<UiImage>()) {
            return child;
        }
    }
    return kUiNone;
}

namespace {

void hostText(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis) {
    if (s.ent[id].try_get<UiText>() != nullptr) {
        return;
    }
    UiText host{};
    host.align = align;
    host.ellipsis = ellipsis;
    host.valign = UiTextValign::Center;
    s.ent[id].set<UiText>(host);
}

void adornButton(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Center, UiTextEllipsis::End);
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void adornClose(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Center, UiTextEllipsis::End);
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void adornCheck(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Left, UiTextEllipsis::End);
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        if (flex->padL < 32.0f) {
            flex->padL = 32.0f;
        }
    }
}

void finishCheck(UiState& s, uint16_t id, const BinNode* spec, const char*, const char*) {
    uint8_t on = 0;
    uint8_t kind = 0;
    uint8_t group = 0;
    if (spec != nullptr) {
        on = (spec->flags & kBinCheckOn) != 0 ? 1 : 0;
        kind = spec->checkKind;
        group = spec->checkGroup;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    uiBindCheck(s, id, kind, group, on != 0);
}

} // namespace

void uiButtonBin(UiState& s) {
    uiBinRole(s, kBinSlotButton, "button", static_cast<uint8_t>(UiToken::Button), kBinRoleRaised);
    uiBinRole(s, kBinSlotCheck, "check", static_cast<uint8_t>(UiToken::Check), 0);
    uiBinRole(s, kBinSlotClose, "close", static_cast<uint8_t>(UiToken::Close), 0);
    uiRegisterSpawnHook(s, kBinSlotButton, adornButton);
    uiRegisterSpawnHook(s, kBinSlotClose, adornClose);
    uiRegisterSpawnHook(s, kBinSlotCheck, adornCheck);
    uiRegisterRoleFinish(s, kBinSlotCheck, finishCheck);
}

} // namespace burnhope

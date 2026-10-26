#include "ui/widget/Button.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"

namespace burnhope {

namespace {

constexpr float kButtonIcon = 16.0f;

} // namespace

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg) {
    const bool hasLabel = cfg.label != nullptr && cfg.label[0] != '\0';
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
    const float padX = cfg.padX >= 0.0f ? cfg.padX : s.metrics.buttonPadX;
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
        if (cfg.iconSlot < kPhotoSlots && (cfg.label == nullptr || cfg.label[0] == '\0')) {
            flex.widthMode = UiSize::Px;
            flex.width = kButtonIcon + padX * 2.0f;
            flex.padL = 0.0f;
            flex.padR = 0.0f;
        }
    }
    const uint16_t id = uiPanel(s, parent, flex, paintButton(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, cfg.label != nullptr ? cfg.label : "");
    uiTextStyle(s, id, UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiHover(s, id);
    uiSetFocusable(s, id);
    if (cfg.iconSlot < kPhotoSlots) {
        s.ent[id].set<UiImage>({.slot = cfg.iconSlot, .frames = 1, .inset = kButtonIcon});
    }
    const bool glyph = cfg.icon != UiIcon::None;
    if (glyph && !hasLabel) {
        if (auto* box = s.ent[id].try_get_mut<UiFlex>()) {
            box->pad = 0.0f;
            box->padL = 0.0f;
            box->padR = 0.0f;
            box->padT = 0.0f;
            box->padB = 0.0f;
        }
    }
    if (cfg.ghost) {
        s.ent[id].add<UiIconChrome>();
    }
    if (auto* paint = (glyph || cfg.ghost) ? s.ent[id].try_get_mut<UiPaint>() : nullptr) {
        paint->icon = static_cast<uint8_t>(cfg.icon);
        if (cfg.ghost) {
            paintGhost(*paint);
            paint->plate = 1;
            paint->r = s.look.text[0];
            paint->g = s.look.text[1];
            paint->b = s.look.text[2];
        }
    }
    if (cfg.onClick != nullptr) {
        uiBind(s, id, cfg.onClick, cfg.user);
    }
    return id;
}

uint16_t uiIconBtn(UiState& s, uint16_t parent, uint8_t icon, float side, uint16_t* glyph) {
    const uint16_t id = uiButton(s, parent, {.label = "", .width = side, .height = side, .icon = static_cast<UiIcon>(icon), .ghost = true});
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
        paintTint(*paint, kUiWhite);
        paint->plate = 1;
    }
    if (auto* image = s.ent[id].try_get_mut<UiImage>()) {
        image->inset = side;
    }
    if (icon != nullptr) {
        *icon = id;
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

void uiToolButtonState(UiState& s, uint16_t id, bool active, bool ghost) {
    if (id >= s.count) {
        return;
    }
    uiButtonTintIcon(s, id, active);
    if (ghost) {
        s.ent[id].add<UiIconChrome>();
    }
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paint->bright = active ? 1.0f : 0.7f;
        if (ghost) {
            paintGhost(*paint);
        }
    }
}

float uiButtonTextWidth(const UiState& s, const char* text, float extraPad) {
    const char* label = text != nullptr ? text : "";
    uint8_t len = 0;
    while (label[len] != '\0' && len < 48) {
        ++len;
    }
    return uiMeasure(s, label, len, len) + extraPad;
}

float uiButtonFitText(UiState& s, uint16_t id, const char* text, float extraPad) {
    const float width = uiButtonTextWidth(s, text, extraPad);
    if (id < s.count) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = width;
            flex->grow = 0.0f;
            flex->shrink = 0.0f;
        }
        uiNotifyLayoutDirty(s);
    }
    return width;
}

uint16_t uiButtonIconChild(const UiState& s, uint16_t id) {
    const UiImage* image = id < s.count ? s.ent[id].try_get<UiImage>() : nullptr;
    return image != nullptr && image->inset > 0.0f ? id : kUiNone;
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
    uiSetFocusable(s, id);
}

void adornClose(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Center, UiTextEllipsis::End);
    uiHover(s, id);
    uiSetFocusable(s, id);
}

} // namespace

void uiButtonMark(UiState& s, uint16_t id, bool on) {
    if (id >= s.count) {
        return;
    }
    auto* live = s.ent[id].try_get_mut<UiPaint>();
    if (live == nullptr) {
        return;
    }
    live->token = on ? UiToken::Accent : UiToken::Button;
    uiNotifyVisualDirty(s);
}

void uiButtonBin(UiState& s) {
    uiBinRole(s, kBinSlotButton, "button", static_cast<uint8_t>(UiToken::Button), kBinRoleRaised);
    uiBinRole(s, kBinSlotClose, "close", static_cast<uint8_t>(UiToken::Close), 0);
    uiRegisterSpawnHook(s, kBinSlotButton, adornButton);
    uiRegisterSpawnHook(s, kBinSlotClose, adornClose);
}

} // namespace burnhope

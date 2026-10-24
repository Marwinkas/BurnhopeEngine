#include "ui/widget/Button.hpp"

#include "ui/widget/Image.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
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

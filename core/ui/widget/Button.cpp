#include "ui/widget/Button.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/Text.hpp"

namespace burnhope {

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg) {
    const char* cls = cfg.cls != nullptr ? cfg.cls : "btn_default";
    const uint16_t id = uiSpawnTemplate(s, parent, "core/button", cls);
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, cfg.label != nullptr ? cfg.label : "");
    if (cfg.icon != UiIcon::None) {
        uiSetIcon(s, id, cfg.icon);
    }
    if (cfg.iconSlot < kPhotoSlots) {
        s.ent[id].set<UiImage>({.slot = cfg.iconSlot, .frames = 1, .inset = uiStyle(s, cls, UiStyleKey::Icon)});
    }
    if (cfg.onClick != nullptr) {
        uiBind(s, id, cfg.onClick, cfg.user);
    }
    return id;
}

uint16_t uiButtonIconChild(const UiState& s, uint16_t id) {
    const UiImage* image = id < s.count ? s.ent[id].try_get<UiImage>() : nullptr;
    return image != nullptr && image->inset > 0.0f ? id : kUiNone;
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

namespace {

// A button read from a binary layout is a plain node: give it the button's text style, hover and focus.
void adornButton(UiState& s, uint16_t id) {
    if (s.ent[id].try_get<UiText>() == nullptr) {
        UiText host{};
        host.align = UiTextAlign::Center;
        host.ellipsis = UiTextEllipsis::End;
        host.valign = UiTextValign::Center;
        s.ent[id].set<UiText>(host);
    }
    uiHover(s, id);
    uiSetFocusable(s, id);
}

} // namespace

void uiButtonBin(UiState& s) {
    uiBinRole(s, kBinSlotButton, "button", static_cast<uint8_t>(UiToken::Button), kBinRoleRaised);
    uiBinRole(s, kBinSlotClose, "close", static_cast<uint8_t>(UiToken::Close), 0);
    uiRegisterSpawnHook(s, kBinSlotButton, adornButton);
    uiRegisterSpawnHook(s, kBinSlotClose, adornButton);
}

} // namespace burnhope

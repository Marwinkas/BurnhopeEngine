#include "ui/widget/Button.hpp"
#include "ui/widget/Image.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
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
        flex.height = cfg.height > 0.0f ? cfg.height : 28.0f;
    }
    
    if (cfg.fillWidth) {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
        flex.grow = 1.0f;
    } else if (cfg.width > 0.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = cfg.width;
    }
    const uint16_t id = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Button, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, cfg.label != nullptr ? cfg.label : "");
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

uint16_t uiButton(UiState& s, uint16_t parent, const char* label, UiClickFn fn, void* user) {
    UiButtonConfig cfg{};
    cfg.label = label;
    cfg.onClick = fn;
    cfg.user = user;
    return uiButton(s, parent, cfg);
}

uint16_t uiIconButton(UiState& s, uint16_t parent, uint8_t iconSlot, UiClickFn fn, void* user) {
    UiButtonConfig cfg{};
    cfg.width = 28.0f;
    cfg.height = 28.0f;
    cfg.iconSlot = iconSlot;
    cfg.onClick = fn;
    cfg.user = user;
    return uiButton(s, parent, cfg);
}

uint16_t uiCheck(UiState& s, uint16_t parent, const char* text, uint8_t kind, uint8_t group, bool on) {
    const uint16_t id = uiPanel(s, parent, {}, paintToken(s.look, UiToken::Check, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, text != nullptr ? text : "");
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    uiBindCheck(s, id, kind, group, on);
    return id;
}

static void onCheck(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
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
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->widthMode = UiSize::Percent;
        flex->width = 100.0f;
        flex->heightMode = UiSize::Px;
        flex->height = 32.0f;
    }
    s.layoutDirty = true;
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

} // namespace burnhope

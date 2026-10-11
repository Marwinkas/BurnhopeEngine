#include "ui/widget/Detail.hpp"

namespace burnhope {

void activate(UiState& s, uint16_t id) {
    const UiPaint* p = s.ent[id].try_get<UiPaint>();
    if (p == nullptr || p->disabled != 0) {
        return;
    }
    if (p->role == static_cast<uint8_t>(UiRole::Check)) {
        auto* check = s.ent[id].try_get_mut<UiCheck>();
        if (check != nullptr) {
            if (check->kind == 1) {
                check->on = 1;
                for (uint16_t other = 0; other < s.count; ++other) {
                    if (other == id) {
                        continue;
                    }
                    auto* mate = s.ent[other].try_get_mut<UiCheck>();
                    if (mate != nullptr && mate->kind == 1 && mate->group == check->group) {
                        mate->on = 0;
                    }
                }
            } else {
                check->on = check->on == 0 ? 1 : 0;
            }
        }
        s.visualDirty = true;
    }
    uiTrace(s, "activate", id);
    if (const UiAction* act = s.ent[id].try_get<UiAction>(); act != nullptr && act->fn != nullptr) {
        act->fn(act->user, id, p->tag);
        return;
    }
    if (p->role == static_cast<uint8_t>(UiRole::Button) && s.onClick != nullptr) {
        s.onClick(s.clickUser, id, p->tag);
    }
}

uint16_t uiButton(UiState& s, uint16_t parent, const UiButtonConfig& cfg) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    flex.align = 1;
    flex.justify = 1;
    if (cfg.height < 0.0f) {
        flex.heightMode = static_cast<uint8_t>(UiSize::Percent);
        flex.height = 100.0f;
    } else {
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = cfg.height > 0.0f ? cfg.height : 28.0f;
    }
    if (cfg.width > 0.0f) {
        flex.widthMode = static_cast<uint8_t>(UiSize::Px);
        flex.width = cfg.width;
    } else if (cfg.width < 0.0f) {
        flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex.width = 100.0f;
        flex.grow = 1.0f;
    }
    const uint16_t id = spawn(s, parent, flex, paintToken(s.look, UiRole::Button, UiToken::Button, s.look.radiusSm, -100));
    if (id == kUiNone) {
        return id;
    }
    addText(s, id, cfg.label != nullptr ? cfg.label : "");
    if (cfg.iconSlot < kPhotoSlots) {
        (void)uiIcon(s, id, cfg.iconSlot, 16.0f, 16.0f);
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
    UiFlex flex{};
    flex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    flex.width = 100.0f;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = 32.0f;
    flex.shrink = 0.0f;
    flex.align = 1;
    const uint16_t id = spawn(s, parent, flex, paintToken(s.look, UiRole::Check, UiToken::Check, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    addText(s, id, text != nullptr ? text : "");
    s.ent[id].set<UiCheck>({on ? uint8_t{1} : uint8_t{0}, kind, group});
    return id;
}

void uiBindCheck(UiState& s, uint16_t id, uint8_t kind, uint8_t group, bool on) {
    if (id >= s.count) {
        return;
    }
    s.ent[id].set<UiCheck>({on ? uint8_t{1} : uint8_t{0}, kind, group});
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex->width = 100.0f;
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = 32.0f;
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

int uiCheckOn(const UiState& s, const char* name) {
    if (name == nullptr) {
        return -1;
    }
    const uint16_t id = uiFindName(s, name);
    if (id == kUiNone || id >= s.count) {
        return -1;
    }
    const UiCheck* check = s.ent[id].try_get<UiCheck>();
    if (check == nullptr) {
        return -1;
    }
    return check->on != 0 ? 1 : 0;
}

} // namespace burnhope

#include "ui/widget/Detail.hpp"

#include <span>

namespace burnhope {
namespace {

void onItem(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr) {
        return;
    }
    const uint16_t menu = uiFindName(*s, "ui.menu");
    auto* pop = menu < s->count ? s->ent[menu].try_get_mut<UiMenuPop>() : nullptr;
    if (pop == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < pop->n; ++i) {
        if (pop->row[i] != id || pop->fn[i] == nullptr) {
            continue;
        }
        void (*fn)(void*) = pop->fn[i];
        void* who = pop->user;
        uiMenuClose(*s);
        fn(who);
        return;
    }
}

uint16_t ensureMenu(UiState& s) {
    uint16_t id = uiFindName(s, "ui.menu");
    if (id != kUiNone) {
        return id;
    }
    UiFlex flex{};
    flex.position = 1;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = 220.0f;
    flex.pad = 4.0f;
    flex.gap = 2.0f;
    flex.shrink = 0.0f;
    id = spawn(s, 0, flex, paintToken(s.look, UiRole::Panel, UiToken::Menu, s.look.radius));
    uiName(s, id, "ui.menu");
    uiShow(s, id, false);
    s.ent[id].set<UiMenuPop>({});
    return id;
}

} // namespace

void uiMenuClose(UiState& s) {
    const uint16_t id = uiFindName(s, "ui.menu");
    if (id != kUiNone) {
        uiShow(s, id, false);
    }
    if (uiMenuMetrics(s).opened == id) {
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }
}

void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user) {
    const uint16_t id = ensureMenu(s);
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* pop = s.ent[id].try_get_mut<UiMenuPop>();
    if (pop == nullptr) {
        return;
    }
    pop->user = user;
    pop->n = 0;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id) {
            uiShow(s, child, false);
        }
    }
    uint8_t slot = 0;
    for (const UiMenuItem& item : items) {
        if (slot >= 12) {
            break;
        }
        uint16_t row = kUiNone;
        uint8_t seen = 0;
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] != id) {
                continue;
            }
            if (seen == slot) {
                row = child;
                break;
            }
            ++seen;
        }
        if (row == kUiNone) {
            if (item.separator) {
                row = spawn(s, id, px(200.0f, 1.0f), paintToken(s.look, UiRole::Panel, UiToken::Muted, 0.0f));
            } else {
                UiFlex line = px(200.0f, 28.0f);
                line.shrink = 0.0f;
                row = spawn(s, id, line, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm));
                addText(s, row, item.label != nullptr ? item.label : "");
                uiBind(s, row, onItem, &s);
            }
        }
        uiShow(s, row, true);
        if (!item.separator) {
            uiText(s, row, item.label != nullptr ? item.label : "");
            if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
                paint->disabled = item.disabled ? 1 : 0;
            }
        }
        pop->row[slot] = row;
        pop->fn[slot] = item.separator ? nullptr : item.onClick;
        ++slot;
    }
    pop->n = slot;
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->position = 1;
        flex->posX = screenX;
        flex->posY = screenY;
        flex->height = static_cast<float>(slot) * 30.0f + 8.0f;
    }
    uiShow(s, id, true);
    uiMenuMetrics(s).opened = id;
    s.menuOpen = true;
    uiMarkLayout(s, id);
}

uint16_t uiMakeMenu(UiState& s) {
    if (s.count == 0) {
        return kUiNone;
    }
    UiFlex flex{};
    flex.position = 1;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = uiMenuMetrics(s).w;
    flex.pad = uiMenuMetrics(s).pad;
    flex.gap = uiMenuMetrics(s).gap;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.role = static_cast<uint8_t>(UiRole::Panel);
    paint.token = static_cast<uint8_t>(UiToken::Menu);
    paint.r = s.look.menu[0];
    paint.g = s.look.menu[1];
    paint.b = s.look.menu[2];
    paint.a = 1.0f;
    paint.radius = s.look.radiusSm;
    const uint16_t id = uiNode(s, 0, flex, paint);
    if (id != kUiNone && s.yoga[id] != nullptr) {
        YGNodeStyleSetDisplay(s.yoga[id], YGDisplayNone);
    }
    return id;
}

void uiMenuStyle(UiState& s, float width, float itemH, float pad, float gap) {
    if (s.menu == kUiNone) {
        return;
    }
    uiMenuMetrics(s).w = width;
    uiMenuMetrics(s).itemH = itemH;
    uiMenuMetrics(s).pad = pad;
    uiMenuMetrics(s).gap = gap;
    auto* flex = s.ent[s.menu].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->width = width;
        flex->pad = pad;
        flex->gap = gap;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.parentOf[id] != s.menu) {
            continue;
        }
        auto* item = s.ent[id].try_get_mut<UiFlex>();
        if (item != nullptr) {
            item->height = itemH;
        }
    }
    s.layoutDirty = true;
}

} // namespace burnhope

#include "ui/widget/Detail.hpp"
#include "ui/widget/Menu.hpp"

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
        if (slot >= kUiMenuCap) {
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
        const float full = static_cast<float>(slot) * 30.0f + 8.0f;
        const float limit = s.laidH > 64.0f ? s.laidH - 16.0f : full;
        flex->height = full < limit ? full : limit;
        flex->overflow = full > limit ? 2 : 0;
    }
    if (slot > 0 && s.ent[id].try_get<UiFlex>() != nullptr && s.ent[id].try_get<UiFlex>()->overflow != 0) {
        s.ent[id].set<UiScroll>({0.0f, id});
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
    if (id != kUiNone) {
        uiShow(s, id, false);
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

void uiMenuShowAt(UiState& s, uint16_t menuId, float x, float y) {
    if (menuId >= s.count) {
        return;
    }
    auto* flex = s.ent[menuId].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    const uint32_t items = uiNodeChildCount(s, menuId);
    const UiMenuMetrics& metrics = uiMenuMetrics(s);
    const float menuH = metrics.pad * 2.0f + static_cast<float>(items) * metrics.itemH
        + static_cast<float>(items > 0 ? items - 1 : 0) * metrics.gap;
    if (x + metrics.w > s.laidW) {
        x = s.laidW - metrics.w;
    }
    if (y + menuH > s.laidH) {
        y = s.laidH - menuH;
    }
    if (x < 4.0f) {
        x = 4.0f;
    }
    if (y < 4.0f) {
        y = 4.0f;
    }
    flex->position = 1;
    flex->posX = x;
    flex->posY = y;
    uiMenuMetrics(s).opened = menuId;
    s.menuOpen = true;
    uiShow(s, menuId, true);
    const UiPaint* menuPaint = s.ent[menuId].try_get<UiPaint>();
    if (menuPaint != nullptr) {
        uiMotionTo(s, menuId, menuPaint->r, menuPaint->g, menuPaint->b, 1.0f);
        s.anim[menuId].fa = 0.0f;
    }
}

} // namespace burnhope

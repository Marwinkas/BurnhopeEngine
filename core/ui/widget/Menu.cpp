#include "ui/widget/Detail.hpp"
#include "ui/widget/Menu.hpp"

#include <algorithm>
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

void menuShell(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        s.ent[id].set<UiPaint>(paintToken(s.look, UiRole::Panel, UiToken::Window, s.look.radiusSm));
        paint = s.ent[id].try_get_mut<UiPaint>();
        if (paint == nullptr) {
            return;
        }
    }
    const float radius = paint->radius;
    *paint = paintToken(s.look, UiRole::Panel, UiToken::Menu, radius);
    paint->a = 1.0f;
    paint->borderW = 1.0f;
    paint->br = s.look.muted[0];
    paint->bg = s.look.muted[1];
    paint->bb = s.look.muted[2];
}

uint16_t ensureMenuNamed(UiState& s, const char* name) {
    uint16_t id = uiFindName(s, name);
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
    id = spawn(s, 0, flex, paintToken(s.look, UiRole::Panel, UiToken::Window, s.look.radius));
    menuShell(s, id);
    uiName(s, id, name);
    uiShow(s, id, false);
    s.ent[id].set<UiMenuPop>({});
    s.ent[id].set<UiPopup>({});
    return id;
}

} // namespace

void uiMarkMenuBar(UiState& s, uint16_t id) {
    if (id < s.count) {
        s.ent[id].set<UiMenuBar>({});
    }
}

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
    const uint16_t id = ensureMenuNamed(s, "ui.menu");
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* pop = s.ent[id].try_get_mut<UiMenuPop>();
    if (pop == nullptr) {
        return;
    }
    pop->user = user;
    pop->n = 0;
    pop->hover = 255;
    pop->sub = kUiNone;
    uiShow(s, uiFindName(s, "ui.menu.sub"), false);
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
                line.align = 2;
                line.justify = 0;
                line.padL = 8.0f;
                row = spawn(s, id, line, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm));
                if (auto* paint = row < s.count ? s.ent[row].try_get_mut<UiPaint>() : nullptr) {
                    paint->a = 0.0f;
                    paint->borderW = 0.0f;
                }
                addText(s, row, item.label != nullptr ? item.label : "");
                uiBind(s, row, onItem, &s);
            }
        }
        uiShow(s, row, true);
        if (!item.separator) {
            uiText(s, row, item.label != nullptr ? item.label : "");
            if (auto* line = s.ent[row].try_get_mut<UiFlex>()) {
                line->align = 2;
                line->justify = 0;
                line->padL = 8.0f;
            }
            if (auto* text = s.ent[row].try_get_mut<UiText>()) {
                text->align = 0;
            }
            if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
                paint->token = static_cast<uint8_t>(UiToken::Item);
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->disabled = item.disabled ? 1 : 0;
            }
        }
        pop->row[slot] = row;
        pop->fn[slot] = item.separator ? nullptr : item.onClick;
        pop->item[slot] = item.separator ? nullptr : &item;
        ++slot;
    }
    pop->n = slot;
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->position = 1;
        const float menuW = flex->width > 1.0f ? flex->width : 220.0f;
        const float full = static_cast<float>(slot) * 30.0f + 8.0f;
        const float limit = s.laidH > 64.0f ? s.laidH - 16.0f : full;
        const float menuH = full < limit ? full : limit;
        float x = screenX;
        float y = screenY;
        if (x + menuW > s.laidW - 4.0f) {
            x = s.laidW - menuW - 4.0f;
        }
        if (y + menuH > s.laidH) {
            y = std::max(4.0f, screenY - menuH);
        }
        if (x < 4.0f) {
            x = 4.0f;
        }
        if (y < 4.0f) {
            y = 4.0f;
        }
        flex->posX = x;
        flex->posY = y;
        flex->height = menuH;
        flex->overflow = full > limit ? 2 : 0;
        menuShell(s, id);
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
    const uint16_t id = uiNode(s, 0, flex, paintToken(s.look, UiRole::Panel, UiToken::Window, s.look.radiusSm));
    if (id != kUiNone) {
        menuShell(s, id);
        uiShow(s, id, false);
        s.ent[id].set<UiPopup>({});
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
    const float anchorY = y;
    y = anchorY + 4.0f;
    if (x + metrics.w > s.laidW) {
        x = s.laidW - metrics.w;
    }
    if (y + menuH > s.laidH) {
        y = anchorY - menuH - 4.0f;
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
    menuShell(s, menuId);
    uiMenuMetrics(s).opened = menuId;
    s.menuOpen = true;
    uiShow(s, menuId, true);
    const UiPaint* menuPaint = s.ent[menuId].try_get<UiPaint>();
    if (menuPaint != nullptr) {
        uiMotionTo(s, menuId, menuPaint->r, menuPaint->g, menuPaint->b, 1.0f);
        s.anim[menuId].fa = 0.0f;
    }
}

namespace {

void onMenuCommand(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    if (tag >= 100) {
        const uint8_t index = static_cast<uint8_t>(tag - 100);
        if (index < s.cmdCount && s.cmds[index].fn != nullptr) {
            s.cmds[index].fn(s.cmds[index].user);
        }
    }
    if (uiMenuMetrics(s).opened != kUiNone) {
        uiShow(s, uiMenuMetrics(s).opened, false);
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }
    s.visualDirty = true;
}

} // namespace

bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user) {
    if (s.cmdCount >= kUiCmdCap || s.menu == kUiNone) {
        return false;
    }
    UiCommand& cmd = s.cmds[s.cmdCount];
    copyText(cmd.name, cmd.len, name);
    cmd.fn = fn;
    cmd.user = user;
    UiFlex itemF{};
    itemF.heightMode = static_cast<uint8_t>(UiSize::Px);
    itemF.height = uiMenuMetrics(s).itemH;
    itemF.widthMode = static_cast<uint8_t>(UiSize::Percent);
    itemF.width = 100.0f;
    itemF.shrink = 0.0f;
    itemF.align = 2;
    itemF.justify = 0;
    itemF.padL = 8.0f;
    const uint16_t item = spawn(s, s.menu, itemF, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm, tag));
    if (auto* rowPaint = item < s.count ? s.ent[item].try_get_mut<UiPaint>() : nullptr) {
        rowPaint->a = 0.0f;
        rowPaint->borderW = 0.0f;
    }
    UiText label{};
    label.align = 0;
    copyText(label.bytes, label.len, name);
    s.ent[item].set<UiText>(label);
    uiBind(s, item, onMenuCommand, &s);
    ++s.cmdCount;
    s.layoutDirty = true;
    return item != kUiNone;
}

bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t hit, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (phase == kUiInEscape) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            uiShow(s, uiMenuMetrics(s).opened, false);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            s.layoutDirty = true;
        }
        return false;
    }
    if (phase != kUiInMenu) {
        return false;
    }
    const bool press = (in.pointer.pressed & kPointerLeft) != 0;
    const bool right = (in.pointer.pressed & kPointerRight) != 0;
    auto inMenu = [&](uint16_t id) {
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (p == uiMenuMetrics(s).opened) {
                return true;
            }
        }
        return false;
    };
    uint16_t contextMenu = s.menu;
    for (uint16_t p = hit; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.contextOf[p] != kUiNone) {
            contextMenu = s.contextOf[p];
            break;
        }
    }
    bool barOpen = false;
    float menuX = in.pointer.x;
    float menuY = in.pointer.y;
    uint16_t bar = kUiNone;
    if (hit != kUiNone && hit < s.count) {
        for (uint16_t p = hit; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].has<UiMenuBar>() && s.contextOf[p] != kUiNone) {
                bar = p;
                break;
            }
        }
    }
    if (press && bar != kUiNone) {
        barOpen = true;
        contextMenu = s.contextOf[bar];
        menuX = s.box[bar].x;
        menuY = s.box[bar].y + s.box[bar].h;
    }
    bool justOpened = false;
    if (right && hit != kUiNone && hit < s.count) {
        const UiPaint* node = s.ent[hit].try_get<UiPaint>();
        if (node != nullptr && node->role == static_cast<uint8_t>(UiRole::Field) && s.fieldMenu != kUiNone) {
            contextMenu = s.fieldMenu;
            uiFocus(s, hit);
            s.editField = hit;
            s.visualDirty = true;
        }
    }
    bool barOnly = false;
    if (right && hit != kUiNone && hit < s.count) {
        for (uint16_t p = hit; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].has<UiMenuBar>()) {
                barOnly = true;
                break;
            }
        }
    }
    if (((right && !barOnly) || barOpen) && contextMenu != kUiNone) {
        if (barOpen && s.menuOpen && uiMenuMetrics(s).opened == contextMenu) {
            uiShow(s, uiMenuMetrics(s).opened, false);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            justOpened = true;
        } else if (uiMenuMetrics(s).opened != kUiNone && uiMenuMetrics(s).opened != contextMenu) {
            uiShow(s, uiMenuMetrics(s).opened, false);
        }
        if (!justOpened) {
            justOpened = true;
            uiMenuShowAt(s, contextMenu, menuX, menuY);
        }
    }
    if (press && s.menuOpen && !justOpened && !inMenu(hit)) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            uiShow(s, uiMenuMetrics(s).opened, false);
        }
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }
    return false;
}

bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user) {
    const uint16_t prev = s.menu;
    s.menu = menu;
    const bool ok = uiCommandAdd(s, name, fn, user);
    s.menu = prev;
    return ok;
}

} // namespace burnhope

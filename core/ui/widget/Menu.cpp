#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Scroll.hpp"

#include <algorithm>
#include <span>

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

void onItem(void* user, uint16_t id) {
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
        s.ent[id].set<UiPaint>(paintToken(s.look, UiToken::Window, s.look.radiusSm));
        paint = s.ent[id].try_get_mut<UiPaint>();
        if (paint == nullptr) {
            return;
        }
    }
    const float radius = paint->radius;
    *paint = paintToken(s.look, UiToken::Menu, radius);
    paint->a = 1.0f;
    paint->borderW = 1.0f;
    paint->br = s.look.text[0];
    paint->bg = s.look.text[1];
    paint->bb = s.look.text[2];
    paint->shadow = 10.0f;
}

uint16_t ensureMenuNamed(UiState& s, const char* name) {
    uint16_t id = uiFindName(s, name);
    if (id != kUiNone) {
        return id;
    }
    UiFlex flex{};
    flex.position = UiPosition::Absolute;
    flex.widthMode = UiSize::Px;
    flex.width = 220.0f;
    flex.pad = 4.0f;
    flex.gap = 2.0f;
    flex.shrink = 0.0f;
    id = spawn(s, 0, flex, paintToken(s.look, UiToken::Window, s.look.radius));
    menuShell(s, id);
    uiName(s, id, name);
    uiShow(s, id, false);
    s.ent[id].set<UiMenuPop>({});
    s.ent[id].set<UiPopup>({});
    return id;
}

float menuTop(const UiState& s, float anchorBottom, float above, float menuH) {
    float top = anchorBottom + 8.0f;
    const bool bar = above >= 0.0f;
    const float buttonTop = bar ? above : anchorBottom;
    if (top + menuH > s.laidH - 4.0f) {
        top = buttonTop - menuH - 10.0f;
    }
    if (bar && top < anchorBottom && top + menuH > buttonTop) {
        top = buttonTop - menuH - 10.0f;
    }
    return top;
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

void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user, float above) {
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
                row = spawn(s, id, px(200.0f, 1.0f), paintToken(s.look, UiToken::Muted, 0.0f));
            } else {
                UiFlex line = px(200.0f, 28.0f);
                line.shrink = 0.0f;
                line.align = UiAlign::Start;
                line.justify = UiJustify::Start;
                line.padL = 8.0f;
                row = spawn(s, id, line, paintToken(s.look, UiToken::Item, s.look.radiusSm));
                if (auto* paint = row < s.count ? s.ent[row].try_get_mut<UiPaint>() : nullptr) {
                    paint->a = 0.0f;
                    paint->borderW = 0.0f;
                }
                addText(s, row, item.label != nullptr ? item.label : "");
                uiTextStyle(s, row, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
                s.ent[row].add<UiFocusable>();
                uiHover(s, row);
                uiBind(s, row, onItem, &s);
            }
        }
        uiShow(s, row, true);
        if (!item.separator) {
            uiText(s, row, item.label != nullptr ? item.label : "");
            if (auto* line = s.ent[row].try_get_mut<UiFlex>()) {
                line->align = UiAlign::Start;
                line->justify = UiJustify::Start;
                line->padL = 8.0f;
            }
            if (auto* text = s.ent[row].try_get_mut<UiText>()) {
                text->align = UiTextAlign::Left;
            }
            if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
                paint->token = UiToken::Item;
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
        flex->position = UiPosition::Absolute;
        const float menuW = flex->width > 1.0f ? flex->width : 220.0f;
        const float full = static_cast<float>(slot) * 30.0f + 8.0f;
        const float limit = s.laidH > 64.0f ? s.laidH - 16.0f : full;
        const float menuH = full < limit ? full : limit;
        float x = screenX;
        float y = screenY;
        if (x + menuW > s.laidW - 4.0f) {
            x = s.laidW - menuW - 4.0f;
        }
        y = menuTop(s, screenY, above, menuH);
        if (x < 4.0f) {
            x = 4.0f;
        }
        flex->posX = x;
        flex->posY = y;
        flex->heightMode = UiSize::Px;
        flex->height = menuH;
        flex->overflow = full > limit ? UiOverflow::Scroll : UiOverflow::Visible;
        menuShell(s, id);
    }
    if (slot > 0 && s.ent[id].try_get<UiFlex>() != nullptr && s.ent[id].try_get<UiFlex>()->overflow != UiOverflow::Visible) {
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
    flex.position = UiPosition::Absolute;
    flex.widthMode = UiSize::Px;
    flex.width = uiMenuMetrics(s).w;
    flex.pad = uiMenuMetrics(s).pad;
    flex.gap = uiMenuMetrics(s).gap;
    flex.shrink = 0.0f;
    const uint16_t id = uiNode(s, 0, flex, paintToken(s.look, UiToken::Window, s.look.radiusSm));
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

void uiMenuShowAt(UiState& s, uint16_t menuId, float x, float y, float above) {
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
    if (x + metrics.w > s.laidW) {
        x = s.laidW - metrics.w;
    }
    y = menuTop(s, anchorY, above, menuH);
    if (x < 4.0f) {
        x = 4.0f;
    }
    flex->position = UiPosition::Absolute;
    flex->heightMode = UiSize::Px;
    flex->height = menuH;
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

void onMenuCommand(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiMenuCmd* cmd = id < s.count ? s.ent[id].try_get<UiMenuCmd>() : nullptr;
    if (cmd != nullptr && cmd->index < s.cmdCount && s.cmds[cmd->index].fn != nullptr) {
        s.cmds[cmd->index].fn(s.cmds[cmd->index].user);
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
        if (s.cmdCount >= kUiCmdCap) {
            spdlog::warn("ui command cap {} full, dropped {}", static_cast<unsigned>(kUiCmdCap), name != nullptr ? name : "");
        }
        return false;
    }
    const uint8_t index = s.cmdCount;
    UiCommand& cmd = s.cmds[index];
    copyText(cmd.name, cmd.len, name);
    cmd.fn = fn;
    cmd.user = user;
    UiFlex itemF{};
    itemF.heightMode = UiSize::Px;
    itemF.height = uiMenuMetrics(s).itemH;
    itemF.widthMode = UiSize::Percent;
    itemF.width = 100.0f;
    itemF.shrink = 0.0f;
    itemF.align = UiAlign::Start;
    itemF.justify = UiJustify::Start;
    itemF.padL = 8.0f;
    const uint16_t item = spawn(s, s.menu, itemF, paintToken(s.look, UiToken::Item, s.look.radiusSm));
    if (auto* rowPaint = item < s.count ? s.ent[item].try_get_mut<UiPaint>() : nullptr) {
        rowPaint->a = 0.0f;
        rowPaint->borderW = 0.0f;
    }
    UiText label{};
    label.align = UiTextAlign::Left;
    label.valign = UiTextValign::Center;
    copyText(label.bytes, label.len, name);
    s.ent[item].set<UiText>(label);
    s.ent[item].set<UiMenuCmd>({index});
    s.ent[item].add<UiFocusable>();
    uiHover(s, item);
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
        if (s.ent[hit].try_get<UiField>() != nullptr && s.fieldMenu != kUiNone) {
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
            uiMenuShowAt(s, contextMenu, menuX, menuY, bar != kUiNone ? s.box[bar].y : -1.0f);
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

uint16_t uiMenuButton(UiState& s, uint16_t parent, const char* label, uint16_t menu) {
    UiFlex flex{};
    flex.direction = UiFlexDir::Row;
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.menuItemH;
    flex.grow = 1.0f;
    flex.shrink = 1.0f;
    flex.align = UiAlign::Center;
    flex.padL = s.metrics.menuPadX;
    flex.padR = 8.0f;
    flex.gap = 6.0f;
    UiPaint body = paintToken(s.look, UiToken::Button, s.look.radiusSm);
    paintRaised(body);
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    UiFlex caption{};
    caption.grow = 1.0f;
    caption.shrink = 1.0f;
    caption.heightMode = UiSize::Px;
    caption.height = 18.0f;
    caption.overflow = UiOverflow::Hidden;
    const uint16_t text = uiPanel(s, id, caption, paintLabel(s.look));
    uiText(s, text, label != nullptr ? label : "");
    uiTextStyle(s, text, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (text < s.count) {
        s.ent[text].add<UiNoHit>();
    }
    UiFlex mark{};
    mark.widthMode = UiSize::Px;
    mark.width = 14.0f;
    mark.heightMode = UiSize::Px;
    mark.height = 14.0f;
    mark.shrink = 0.0f;
    UiPaint glyph{};
    glyph.a = 0.0f;
    glyph.icon = 8;
    glyph.r = s.look.text[0];
    glyph.g = s.look.text[1];
    glyph.b = s.look.text[2];
    const uint16_t icon = uiPanel(s, id, mark, glyph);
    if (icon < s.count) {
        s.ent[icon].add<UiNoHit>();
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    if (menu != kUiNone) {
        uiSetContextMenu(s, id, menu);
        uiMarkMenuBar(s, id);
    }
    return id;
}

namespace {

void choicePick(void* user, uint8_t index) {
    auto& s = *static_cast<UiState*>(user);
    const uint16_t menu = uiMenuMetrics(s).opened;
    if (menu == kUiNone) {
        return;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* choice = s.ent[id].try_get_mut<UiChoice>();
        if (choice == nullptr || choice->menu != menu) {
            continue;
        }
        choice->index = index;
        uint8_t seen = 0;
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] != menu) {
                continue;
            }
            if (seen == index) {
                const UiText* text = s.ent[child].try_get<UiText>();
                if (text != nullptr && choice->caption < s.count) {
                    uiText(s, choice->caption, text->bytes);
                }
                break;
            }
            ++seen;
        }
        if (choice->onPick != nullptr) {
            choice->onPick(choice->user, index);
        }
        return;
    }
}

void pick0(void* user) { choicePick(user, 0); }
void pick1(void* user) { choicePick(user, 1); }
void pick2(void* user) { choicePick(user, 2); }
void pick3(void* user) { choicePick(user, 3); }
void pick4(void* user) { choicePick(user, 4); }
void pick5(void* user) { choicePick(user, 5); }
void pick6(void* user) { choicePick(user, 6); }
void pick7(void* user) { choicePick(user, 7); }

} // namespace

uint16_t uiChoice(UiState& s, uint16_t parent, const UiChoiceConfig& cfg) {
    if (cfg.labels == nullptr || cfg.count == 0 || cfg.count > 8) {
        return kUiNone;
    }
    const uint8_t selected = cfg.selected < cfg.count ? cfg.selected : 0;
    const uint16_t menu = uiMakeMenu(s);
    if (menu == kUiNone) {
        return kUiNone;
    }
    using Pick = void (*)(void*);
    const Pick picks[8] = {pick0, pick1, pick2, pick3, pick4, pick5, pick6, pick7};
    for (uint8_t i = 0; i < cfg.count; ++i) {
        (void)uiCommandAddTo(s, menu, cfg.labels[i] != nullptr ? cfg.labels[i] : "", picks[i], &s);
    }
    const uint16_t id = uiMenuButton(s, parent, cfg.labels[selected], menu);
    if (id == kUiNone) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->widthMode = UiSize::Px;
        flex->width = cfg.width > 48.0f ? cfg.width : 140.0f;
        flex->grow = 0.0f;
        flex->shrink = 0.0f;
    }
    uint16_t caption = kUiNone;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id && s.ent[child].try_get<UiText>() != nullptr) {
            caption = child;
            break;
        }
    }
    s.ent[id].set<UiChoice>({menu, caption, selected, cfg.onPick, cfg.user});
    return id;
}

void uiBindChoice(UiState& s, const char* name, uint8_t selected, void (*onPick)(void* user, uint8_t index), void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* choice = id < s.count ? s.ent[id].try_get_mut<UiChoice>() : nullptr;
    if (choice == nullptr) {
        return;
    }
    choice->onPick = onPick;
    choice->user = user;
    if (selected == choice->index) {
        return;
    }
    choice->index = selected;
    uint8_t seen = 0;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != choice->menu) {
            continue;
        }
        if (seen == selected) {
            const UiText* text = s.ent[child].try_get<UiText>();
            if (text != nullptr && choice->caption < s.count) {
                uiText(s, choice->caption, text->bytes);
            }
            return;
        }
        ++seen;
    }
}

} // namespace burnhope

#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"

#include <span>

namespace burnhope {
namespace {

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

void menuDismiss(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    if (s.ent[id].has<UiMenuSlide>()) {
        uiReveal(s, id, 0.0f, false);
        return;
    }
    uiShow(s, id, false);
}

void armMenu(UiState& s) {
    uiMenuHooks(s).invoke = [](void* user, uint16_t id) {
        auto& state = *static_cast<UiState*>(user);
        const UiMenuCmd* row = id < state.count ? state.ent[id].try_get<UiMenuCmd>() : nullptr;
        UiCommandFn fn = nullptr;
        void* who = nullptr;
        uint16_t item = 0;
        uint16_t owner = kUiNone;
        if (row != nullptr) {
            fn = row->fn;
            who = row->user;
            item = row->item;
            owner = row->owner;
        }
        uiMenuClose(state);
        if (owner != kUiNone && owner < state.count) {
            auto* choice = state.ent[owner].try_get_mut<UiChoice>();
            if (choice != nullptr) {
                choice->index = item > 255 ? uint8_t{255} : static_cast<uint8_t>(item);
                const uint16_t caption = choice->caption < state.count ? choice->caption : owner;
                const UiText* text = id < state.count ? state.ent[id].try_get<UiText>() : nullptr;
                if (text != nullptr && caption < state.count) {
                    uiText(state, caption, text->bytes);
                }
                if (choice->onPick != nullptr) {
                    choice->onPick(choice->user, choice->index);
                }
            }
        }
        if (fn != nullptr) {
            fn(who, item);
        }
        uiNotifyVisualDirty(state);
    };
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

uint16_t nthChild(const UiState& s, uint16_t parent, uint16_t n) {
    uint16_t seen = 0;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != parent) {
            continue;
        }
        if (seen == n) {
            return child;
        }
        ++seen;
    }
    return kUiNone;
}

void silenceRow(UiState& s, uint16_t row) {
    if (row >= s.count) {
        return;
    }
    if (s.ent[row].has<UiMenuCmd>()) {
        s.ent[row].remove<UiMenuCmd>();
    }
    if (s.ent[row].has<UiAction>()) {
        s.ent[row].remove<UiAction>();
    }
    if (s.ent[row].has<UiFocusable>()) {
        s.ent[row].remove<UiFocusable>();
    }
    if (s.ent[row].has<UiHoverable>()) {
        s.ent[row].remove<UiHoverable>();
    }
}

uint16_t separatorRow(UiState& s, uint16_t menu, uint16_t row) {
    if (row == kUiNone) {
        UiFlex line = px(200.0f, 1.0f);
        line.widthMode = UiSize::Percent;
        line.width = 100.0f;
        line.shrink = 0.0f;
        row = spawn(s, menu, line, paintToken(s.look, UiToken::Muted, 0.0f));
    }
    if (row >= s.count) {
        return kUiNone;
    }
    if (auto* flex = s.ent[row].try_get_mut<UiFlex>()) {
        flex->heightMode = UiSize::Px;
        flex->height = 1.0f;
        flex->widthMode = UiSize::Percent;
        flex->width = 100.0f;
        flex->shrink = 0.0f;
    }
    if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
        *paint = paintToken(s.look, UiToken::Muted, 0.0f);
    }
    uiText(s, row, "");
    silenceRow(s, row);
    uiShow(s, row, true);
    return row;
}

uint16_t flyoutMenu(UiState& s) {
    UiMenuMetrics& metrics = uiMenuMetrics(s);
    if (metrics.flyout < s.count && s.ent[metrics.flyout].is_alive()) {
        return metrics.flyout;
    }
    const uint16_t id = uiMakeMenu(s);
    if (id < s.count) {
        s.ent[id].remove<UiMenuSlide>();
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->width = 220.0f;
        }
    }
    metrics.flyout = id;
    return id;
}

const char* choiceLabel(const UiState& s, uint16_t menu, uint8_t index) {
    uint8_t seen = 0;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != menu) {
            continue;
        }
        if (seen == index) {
            const UiText* text = s.ent[child].try_get<UiText>();
            return text != nullptr ? text->bytes : nullptr;
        }
        ++seen;
    }
    return nullptr;
}

} // namespace

void uiMarkMenuBar(UiState& s, uint16_t id) {
    if (id < s.count) {
        s.ent[id].set<UiMenuBar>({});
    }
}

void uiMenuClose(UiState& s) {
    const uint16_t id = uiMenuMetrics(s).opened;
    if (id != kUiNone) {
        menuDismiss(s, id);
    }
    uiMenuMetrics(s).opened = kUiNone;
    s.menuOpen = false;
}

void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user, float above) {
    armMenu(s);
    const uint16_t id = flyoutMenu(s);
    if (id == kUiNone || id >= s.count) {
        return;
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id) {
            uiShow(s, child, false);
        }
    }
    uint16_t slot = 0;
    float body = 0.0f;
    const UiMenuMetrics& metrics = uiMenuMetrics(s);
    for (const UiMenuItem& item : items) {
        const uint16_t existing = nthChild(s, id, slot);
        if (item.separator) {
            if (separatorRow(s, id, existing) == kUiNone) {
                break;
            }
            body += 1.0f;
        } else {
            uint16_t row = existing;
            if (row == kUiNone) {
                row = uiMenuRow(s, id, item.label, item.onClick, user, slot, kUiNone, kUiNone);
            } else {
                uiMenuRowBind(s, row, item.label, item.onClick, user, slot, kUiNone, kUiNone);
            }
            if (row == kUiNone) {
                break;
            }
            if (item.disabled) {
                if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
                    paint->disabled = true;
                }
            }
            uiShow(s, row, true);
            body += metrics.itemH;
        }
        ++slot;
    }
    const float gaps = slot > 0 ? static_cast<float>(slot - 1) * metrics.gap : 0.0f;
    float menuH = metrics.pad * 2.0f + body + gaps;
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->position = UiPosition::Absolute;
        const float menuW = flex->width > 1.0f ? flex->width : 220.0f;
        const float full = menuH;
        const float limit = s.laidH > 64.0f ? s.laidH - 16.0f : full;
        menuH = full < limit ? full : limit;
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
        if (flex->overflow != UiOverflow::Visible) {
            s.ent[id].set<UiScroll>({0.0f, id});
        } else if (s.ent[id].has<UiScroll>()) {
            s.ent[id].remove<UiScroll>();
        }
    }
    uiMenuMetrics(s).opened = id;
    s.menuOpen = true;
    uiShow(s, id, true);
    uiMarkLayout(s, id);
}

uint16_t uiMakeMenu(UiState& s) {
    armMenu(s);
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
        s.ent[id].add<UiMenuSlide>();
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
    uiNotifyLayoutDirty(s);
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
    if (s.ent[menuId].has<UiMenuSlide>()) {
        uiReveal(s, menuId, menuH, true);
    } else {
        uiShow(s, menuId, true);
        const UiPaint* menuPaint = s.ent[menuId].try_get<UiPaint>();
        if (menuPaint != nullptr) {
            uiMotionTo(s, menuId, menuPaint->r, menuPaint->g, menuPaint->b, 1.0f);
            s.anim[menuId].fa = 0.0f;
        }
    }
}

bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t hit, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (phase == kUiInEscape) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            menuDismiss(s, uiMenuMetrics(s).opened);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            uiNotifyLayoutDirty(s);
        }
        return false;
    }
    if (phase != kUiPhase4) {
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
    bool barOnly = false;
    if (right && hit != kUiNone && hit < s.count) {
        for (uint16_t p = hit; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].has<UiMenuBar>()) {
                barOnly = true;
                break;
            }
        }
    }
    bool justOpened = false;
    if (((right && !barOnly) || barOpen) && contextMenu != kUiNone) {
        if (barOpen && s.menuOpen && uiMenuMetrics(s).opened == contextMenu) {
            menuDismiss(s, uiMenuMetrics(s).opened);
            uiMenuMetrics(s).opened = kUiNone;
            s.menuOpen = false;
            justOpened = true;
        } else if (uiMenuMetrics(s).opened != kUiNone && uiMenuMetrics(s).opened != contextMenu) {
            menuDismiss(s, uiMenuMetrics(s).opened);
        }
        if (!justOpened) {
            justOpened = true;
            uiMenuShowAt(s, contextMenu, menuX, menuY, bar != kUiNone ? s.box[bar].y : -1.0f);
        }
    }
    if (press && s.menuOpen && !justOpened && !inMenu(hit)) {
        if (uiMenuMetrics(s).opened != kUiNone) {
            menuDismiss(s, uiMenuMetrics(s).opened);
        }
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }
    return false;
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
    flex.justify = UiJustify::Start;
    flex.padL = s.metrics.menuPadX;
    flex.padR = 22.0f;
    const uint16_t id = uiPanel(s, parent, flex, paintButton(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, label != nullptr ? label : "");
    uiTextStyle(s, id, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
        paint->icon = static_cast<uint8_t>(UiIcon::MenuChevron);
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    if (menu != kUiNone) {
        uiSetContextMenu(s, id, menu);
        uiMarkMenuBar(s, id);
    }
    return id;
}

uint16_t uiChoice(UiState& s, uint16_t parent, const UiChoiceConfig& cfg) {
    if (cfg.labels == nullptr || cfg.count == 0) {
        return kUiNone;
    }
    const uint8_t selected = cfg.selected < cfg.count ? cfg.selected : 0;
    const uint16_t menu = uiMakeMenu(s);
    if (menu == kUiNone) {
        return kUiNone;
    }
    const uint16_t id = uiMenuButton(s, parent, cfg.labels[selected] != nullptr ? cfg.labels[selected] : "", menu);
    if (id == kUiNone) {
        return id;
    }
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        flex->widthMode = UiSize::Px;
        flex->width = cfg.width > 1.0f ? cfg.width : 140.0f;
        flex->grow = 0.0f;
        flex->shrink = 0.0f;
    }
    for (uint8_t i = 0; i < cfg.count; ++i) {
        (void)uiMenuRow(s, menu, cfg.labels[i] != nullptr ? cfg.labels[i] : "", nullptr, nullptr, i, id, kUiNone);
    }
    s.ent[id].set<UiChoice>({
        .menu = menu,
        .caption = id,
        .index = selected,
        .onPick = cfg.onPick,
        .user = cfg.user,
    });
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
    const char* label = choiceLabel(s, choice->menu, selected);
    if (label != nullptr && choice->caption < s.count) {
        uiText(s, choice->caption, label);
    }
}

void uiMenuCaption(UiState& s, uint16_t button, const char* text) {
    if (text == nullptr || button >= s.count) {
        return;
    }
    uiText(s, button, text);
    uiTextStyle(s, button, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
}

float uiChoiceWidth(const UiState& s, const char* const* labels, uint8_t count) {
    float widest = 0.0f;
    if (labels == nullptr) {
        return 26.0f;
    }
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t len = 0;
        const char* label = labels[i];
        while (label != nullptr && label[len] != '\0' && len < 48) {
            ++len;
        }
        const float measured = uiMeasure(s, label, len, len);
        if (measured > widest) {
            widest = measured;
        }
    }
    return widest + 26.0f;
}

void uiChoiceFit(UiState& s, uint16_t button, float width) {
    if (button >= s.count) {
        return;
    }
    if (auto* flex = s.ent[button].try_get_mut<UiFlex>()) {
        flex->widthMode = UiSize::Px;
        flex->width = width;
        flex->minW = 0.0f;
        flex->grow = 0.0f;
        flex->shrink = 0.0f;
        flex->heightMode = UiSize::Px;
        flex->height = 28.0f;
        flex->self = UiAlignSelf::Center;
        flex->padL = 6.0f;
        flex->padR = 22.0f;
        flex->gap = 2.0f;
    }
    const UiChoice* choice = s.ent[button].try_get<UiChoice>();
    if (choice != nullptr && choice->menu < s.count) {
        s.ent[choice->menu].add<UiMenuSlide>();
        if (auto* flex = s.ent[choice->menu].try_get_mut<UiFlex>()) {
            flex->width = width;
        }
    }
}

uint16_t loadChoice(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    if (s.importBag.count == 0) {
        return kUiNone;
    }
    const char* labels[kUiImportItemCap]{};
    for (uint8_t i = 0; i < s.importBag.count; ++i) {
        labels[i] = s.importBag.item[i];
    }
    uint8_t selected = static_cast<uint8_t>(node.value);
    if (selected >= s.importBag.count) {
        selected = 0;
    }
    return uiChoice(s, parent, {
        .labels = labels,
        .count = s.importBag.count,
        .selected = selected,
        .width = node.width > 48.0f ? node.width : 140.0f,
    });
}

void uiMenuBin(UiState& s) {
    armMenu(s);
    uiBinRole(s, kBinSlotMenu, "menu", static_cast<uint8_t>(UiToken::Menu), 0);
    uiBinRole(s, kBinSlotChoice, "choice", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotChoice, loadChoice, nullptr);
}

} // namespace burnhope

#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"

#include <span>

#include <spdlog/spdlog.h>

namespace burnhope {
namespace {

struct UiMenuFlyout {};

UiMenuMetrics metricsOf(const UiState& s, uint16_t menu) {
    if (menu < s.count) {
        if (const UiMenuMetrics* live = s.ent[menu].try_get<UiMenuMetrics>()) {
            return *live;
        }
    }
    return {
        .w = uiStyle(s, "menu", UiStyleKey::Width),
        .itemH = uiStyle(s, "menu", UiStyleKey::RowH),
        .pad = uiStyle(s, "menu", UiStyleKey::Pad),
        .gap = uiStyle(s, "menu", UiStyleKey::Gap),
    };
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
    *paint = paintPopup(s.look);
    paint->shadow = uiStyle(s, "menu", UiStyleKey::Shadow);
}

// A bordered trigger (Choice, dropdown) wears the orange accent border while its menu is open.
void litTrigger(UiState& s, uint16_t menu, bool on) {
    for (uint16_t p = 0; p < s.count; ++p) {
        if (s.contextOf[p] != menu || !s.ent[p].has<UiMenuBar>()) {
            continue;
        }
        auto* paint = s.ent[p].try_get_mut<UiPaint>();
        if (paint == nullptr || paint->borderW < kUiBorderMinW) {
            continue;
        }
        if (on) {
            paintBorderToken(s.look, *paint, UiToken::ToolActive);
        } else {
            paintBorderToken(s.look, *paint, UiToken::Muted);
        }
    }
    uiNotifyVisualDirty(s);
}

void menuDismiss(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    litTrigger(s, id, false);
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
            uiChoiceTake(state, owner, id, item);
        }
        if (fn != nullptr) {
            fn(who, item);
        }
        uiNotifyVisualDirty(state);
    };
}

float menuTop(const UiState& s, float anchorBottom, float above, float menuH) {
    const float offset = uiStyle(s, "menu", UiStyleKey::Offset);
    float top = anchorBottom + uiStyle(s, "menu", UiStyleKey::AnchorGap);
    const bool bar = above >= 0.0f;
    const float buttonTop = bar ? above : anchorBottom;
    if (top + menuH > s.laidH - uiStyle(s, "menu", UiStyleKey::ScreenGap)) {
        top = buttonTop - menuH - offset;
    }
    if (bar && top < anchorBottom && top + menuH > buttonTop) {
        top = buttonTop - menuH - offset;
    }
    return top;
}

uint16_t nthChild(const UiState& s, uint16_t parent, uint16_t n) {
    return n < uiNodeChildCount(s, parent) ? uiNodeChild(s, parent, n) : kUiNone;
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
        uiSetFocusable(s, row, false);
    }
    if (s.ent[row].has<UiHoverable>()) {
        s.ent[row].remove<UiHoverable>();
    }
}

uint16_t separatorRow(UiState& s, uint16_t menu, uint16_t row) {
    const UiTemplateId tpl = uiTemplateFind(s, "core/menu_sep");
    if (row == kUiNone) {
        row = uiSpawnTemplate(s, menu, tpl);
    } else if (row < s.count && !uiApplyTemplate(s, row, tpl)) {
        return kUiNone;
    }
    if (row >= s.count) {
        return kUiNone;
    }
    uiText(s, row, "");
    silenceRow(s, row);
    uiShow(s, row, true);
    return row;
}

uint16_t flyoutMenu(UiState& s) {
    const uint16_t have = uiMarked<UiMenuFlyout>(s);
    if (have < s.count && s.ent[have].is_alive()) {
        return have;
    }
    const uint16_t id = uiMakeMenu(s);
    if (id < s.count) {
        s.ent[id].remove<UiMenuSlide>();
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->width = uiStyle(s, "menu", UiStyleKey::FlyoutW);
        }
    }
    uiMarkOnly<UiMenuFlyout>(s, id);
    return id;
}

uint16_t siblingIndex(const UiState& s, uint16_t menu) {
    const uint32_t n = uiNodeChildCount(s, menu);
    return n > 65535u ? uint16_t{65535} : static_cast<uint16_t>(n);
}

} // namespace

void uiMenuRowBind(UiState& s, uint16_t row, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command) {
    if (row >= s.count) {
        return;
    }
    const float radius = s.ent[row].try_get<UiPaint>() != nullptr ? s.ent[row].try_get<UiPaint>()->radius : 0.0f;
    if (!uiApplyTemplate(s, row, uiTemplateFind(s, "core/menu_item"))) {
        return;
    }
    if (auto* flex = s.ent[row].try_get_mut<UiFlex>()) {
        flex->heightMode = UiSize::Px;
        flex->height = metricsOf(s, s.parentOf[row]).itemH;
    }
    if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
        paint->radius = radius > 0.0f ? radius : paint->radius; // a row that already had a corner keeps it
        paint->disabled = false;
    }
    uiText(s, row, label != nullptr ? label : "");
    uiCommandStore(s, row, fn, user, item, owner, command);
}

uint16_t uiMenuRow(UiState& s, uint16_t menu, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command) {
    if (menu >= s.count) {
        return kUiNone;
    }
    const uint16_t row = uiSpawnTemplate(s, menu, "core/menu_item");
    if (row == kUiNone || row >= s.count) {
        return kUiNone;
    }
    uiMenuRowBind(s, row, label, fn, user, item, owner, command);
    return row;
}

bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user) {
    if (menu == kUiNone || menu >= s.count) {
        return false;
    }
    UiCommandList list{};
    if (const UiCommandList* live = s.ent[menu].try_get<UiCommandList>()) {
        list = *live;
    }
    if (list.count >= kUiCmdCap) {
        spdlog::warn("ui command cap {} full, dropped {}", static_cast<unsigned>(kUiCmdCap), name != nullptr ? name : "");
        return false;
    }
    const uint8_t slot = list.count;
    const uint16_t item = siblingIndex(s, menu);
    const uint16_t row = uiMenuRow(s, menu, name, fn, user, item, kUiNone, slot);
    if (row == kUiNone) {
        return false;
    }
    ++list.count;
    s.ent[menu].set<UiCommandList>(list);
    uiNotifyLayoutDirty(s);
    return true;
}

bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user) {
    return uiCommandAddTo(s, uiMenu(s), name, fn, user);
}

void uiSetMenu(UiState& s, uint16_t id) {
    uiMarkOnly<UiCommandTarget>(s, id);
}

uint16_t uiMenu(const UiState& s) {
    return uiMarked<UiCommandTarget>(s);
}

void uiMenuSetShown(UiState& s, bool shown) {
    if (!shown) {
        uiMarkOnly<UiMenuOpen>(s, kUiNone);
    }
}

bool uiMenuShown(const UiState& s) {
    return uiMarked<UiMenuOpen>(s) != kUiNone;
}

void uiMarkMenuBar(UiState& s, uint16_t id) {
    if (id < s.count) {
        s.ent[id].set<UiMenuBar>({});
    }
}

void uiMenuClose(UiState& s) {
    const uint16_t id = uiMarked<UiMenuOpen>(s);
    if (id != kUiNone) {
        menuDismiss(s, id);
    }
    uiMenuSetShown(s, false);
}

void uiMenuOpen(UiState& s, float screenX, float screenY, std::span<const UiMenuItem> items, void* user, float above) {
    armMenu(s);
    const uint16_t id = flyoutMenu(s);
    if (id == kUiNone || id >= s.count) {
        return;
    }
    const uint32_t kids = uiNodeChildCount(s, id);
    for (uint32_t k = 0; k < kids; ++k) {
        uiShow(s, uiNodeChild(s, id, k), false);
    }
    uint16_t slot = 0;
    float body = 0.0f;
    const UiMenuMetrics metrics = metricsOf(s, id);
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
        const float screenGap = uiStyle(s, "menu", UiStyleKey::ScreenGap);
        const float menuW = flex->width > 1.0f ? flex->width : uiStyle(s, "menu", UiStyleKey::FlyoutW);
        const float full = menuH;
        const float limit = s.laidH > 64.0f ? s.laidH - 16.0f : full;
        menuH = full < limit ? full : limit;
        float x = screenX;
        float y = screenY;
        if (x + menuW > s.laidW - screenGap) {
            x = s.laidW - menuW - screenGap;
        }
        y = menuTop(s, screenY, above, menuH);
        if (x < screenGap) {
            x = screenGap;
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
    uiMarkOnly<UiMenuOpen>(s, id);
    uiMenuSetShown(s, true);
    uiShow(s, id, true);
    uiMarkLayout(s, id);
}

uint16_t uiMakeMenu(UiState& s) {
    armMenu(s);
    if (s.count == 0) {
        return kUiNone;
    }
    const UiMenuMetrics style = metricsOf(s, uiMenu(s));
    const uint16_t id = uiSpawnTemplate(s, 0, "core/menu");
    if (id != kUiNone) {
        uiPlace(s, id, true, 0.0f, 0.0f, style.w, 0.0f);
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->pad = style.pad;
            flex->gap = style.gap;
        }
        menuShell(s, id);
        s.ent[id].set<UiMenuMetrics>(style);
        uiShow(s, id, false);
        s.ent[id].set<UiPopup>({});
        s.ent[id].add<UiMenuSlide>();
    }
    return id;
}

void uiMenuList(UiState& s, uint16_t anchor, uint16_t count, const char* (*labelAt)(void* user, uint16_t index), void (*onPick)(void* user, uint16_t index), void* user) {
    constexpr uint16_t kListCap = 48;
    if (labelAt == nullptr || onPick == nullptr || count == 0 || anchor >= s.count) {
        return;
    }
    const UiBox& below = s.box[anchor];
    const UiMenuMetrics metrics = metricsOf(s, uiMenu(s));
    const float stride = metrics.itemH + metrics.gap;
    const float room = s.laidH > 64.0f ? s.laidH - 32.0f - metrics.pad * 2.0f : 0.0f;
    uint16_t fit = room > stride ? static_cast<uint16_t>(room / stride) : uint16_t{1};
    fit = fit < kListCap ? fit : kListCap;
    fit = fit < count ? fit : count;
    UiMenuItem items[kListCap]{};
    for (uint16_t i = 0; i < fit; ++i) {
        items[i].label = labelAt(user, i);
        items[i].onClick = onPick;
    }
    uiMenuOpen(s, below.x, below.y + below.h, std::span<const UiMenuItem>(items, fit), user);
}

void uiMenuStyle(UiState& s, float width, float itemH, float pad, float gap) {
    const uint16_t menu = uiMenu(s);
    if (menu == kUiNone) {
        return;
    }
    s.ent[menu].set<UiMenuMetrics>({width, itemH, pad, gap});
    auto* flex = s.ent[menu].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->width = width;
        flex->pad = pad;
        flex->gap = gap;
    }
    const uint32_t kids = uiNodeChildCount(s, menu);
    for (uint32_t k = 0; k < kids; ++k) {
        const uint16_t child = uiNodeChild(s, menu, k);
        auto* item = child < s.count ? s.ent[child].try_get_mut<UiFlex>() : nullptr;
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
    const UiMenuMetrics metrics = metricsOf(s, menuId);
    // A popup never narrows below the "menu" class minW, whatever trigger width it was sized from.
    const float minW = uiStyle(s, "menu", UiStyleKey::PopupMinW);
    if (flex->width < minW) {
        flex->width = minW;
    }
    const float menuH = metrics.pad * 2.0f + static_cast<float>(items) * metrics.itemH
        + static_cast<float>(items > 0 ? items - 1 : 0) * metrics.gap;
    const float anchorY = y;
    const float shownW = flex->width > metrics.w ? flex->width : metrics.w;
    if (x + shownW > s.laidW) {
        x = s.laidW - shownW;
    }
    y = menuTop(s, anchorY, above, menuH);
    const float edge = uiStyle(s, "menu", UiStyleKey::ScreenGap);
    if (x < edge) {
        x = edge;
    }
    flex->position = UiPosition::Absolute;
    flex->heightMode = UiSize::Px;
    flex->height = menuH;
    flex->posX = x;
    flex->posY = y;
    menuShell(s, menuId);
    litTrigger(s, menuId, true);
    uiMarkOnly<UiMenuOpen>(s, menuId);
    uiMenuSetShown(s, true);
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

bool uiMenuInput(UiState& s, const InputFrame& in, uint16_t hit, uint8_t phase, UiPhaseNote& note) {
    (void)note;
    if (phase == kUiInEscape) {
        if (uiMarked<UiMenuOpen>(s) != kUiNone) {
            menuDismiss(s, uiMarked<UiMenuOpen>(s));
            uiMarkOnly<UiMenuOpen>(s, kUiNone);
            uiMenuSetShown(s, false);
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
            if (p == uiMarked<UiMenuOpen>(s)) {
                return true;
            }
        }
        return false;
    };
    uint16_t contextMenu = uiMenu(s);
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
        if (barOpen && uiMenuShown(s) && uiMarked<UiMenuOpen>(s) == contextMenu) {
            menuDismiss(s, uiMarked<UiMenuOpen>(s));
            uiMarkOnly<UiMenuOpen>(s, kUiNone);
            uiMenuSetShown(s, false);
            justOpened = true;
        } else if (uiMarked<UiMenuOpen>(s) != kUiNone && uiMarked<UiMenuOpen>(s) != contextMenu) {
            menuDismiss(s, uiMarked<UiMenuOpen>(s));
        }
        if (!justOpened) {
            justOpened = true;
            uiMenuShowAt(s, contextMenu, menuX, menuY, bar != kUiNone ? s.box[bar].y : -1.0f);
        }
    }
    if (press && uiMenuShown(s) && !justOpened && !inMenu(hit)) {
        if (uiMarked<UiMenuOpen>(s) != kUiNone) {
            menuDismiss(s, uiMarked<UiMenuOpen>(s));
        }
        uiMarkOnly<UiMenuOpen>(s, kUiNone);
        uiMenuSetShown(s, false);
    }
    return false;
}

uint16_t uiMenuButton(UiState& s, uint16_t parent, const char* label, uint16_t menu, const char* blueprint) {
    const uint16_t id = uiSpawnTemplate(s, parent, blueprint);
    if (id == kUiNone) {
        return id;
    }
    uiText(s, id, label != nullptr ? label : "");
    uiSetIcon(s, id, UiIcon::MenuChevron);
    if (menu != kUiNone) {
        uiSetContextMenu(s, id, menu);
        uiMarkMenuBar(s, id);
    }
    return id;
}

void uiMenuCaption(UiState& s, uint16_t button, const char* text) {
    if (text == nullptr || button >= s.count) {
        return;
    }
    uiText(s, button, text);
    uiTextStyle(s, button, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
}

void uiMenuBin(UiState& s) {
    armMenu(s);
    uiBinRole(s, kBinSlotMenu, "menu", static_cast<uint8_t>(UiToken::Menu), 0);
}

} // namespace burnhope

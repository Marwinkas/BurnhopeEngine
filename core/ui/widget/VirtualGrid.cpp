#include "ui/widget/Detail.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/VirtualGrid.hpp"

namespace burnhope {
namespace {

void onCell(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    UiVirtualGrid* grid = uiAncestor<UiVirtualGrid>(*s, id);
    const uint32_t index = uiVirtRowIndex(*s, id);
    if (grid == nullptr || index >= grid->count) {
        return;
    }
    const bool again = grid->last == index;
    grid->last = index;
    if (grid->select != nullptr) {
        grid->select(grid->user, index, false);
    }
    if (again && grid->activate != nullptr) {
        grid->activate(grid->user, index);
    }
}

uint16_t addCell(UiState& s, uint16_t inner, float itemW, float itemH) {
    UiFlex cell = px(itemW > 8.0f ? itemW - 8.0f : itemW, itemH > 8.0f ? itemH - 8.0f : itemH);
    cell.shrink = 0.0f;
    cell.direction = 0;
    cell.align = 1;
    cell.justify = 2;
    cell.pad = 6.0f;
    const uint16_t slot = spawn(s, inner, cell, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm));
    if (slot == kUiNone) {
        return slot;
    }
    const uint16_t icon = uiIcon(s, slot, 0, 48.0f, 48.0f);
    addText(s, slot, " ");
    uiTextStyle(s, slot, 1, 1, 2, 0, 0, 1.0f);
    if (icon != kUiNone) {
        uiNodeMoveChild(s, slot, icon, 0);
    }
    s.ent[slot].set<UiParts>({icon, kUiNone, slot});
    uiBind(s, slot, onCell, &s);
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

float uiGridContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight<UiVirtualGrid>(s, id);
}

uint16_t uiVirtualGrid(UiState& s, uint16_t parent, const UiVirtualGridConfig& cfg) {
    const float itemW = cfg.itemW < 32.0f ? 84.0f : cfg.itemW;
    const float itemH = cfg.itemH < 32.0f ? 96.0f : cfg.itemH;
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiRole::Scroll, UiToken::Content, s.look.radiusSm);
    if (scroll == kUiNone) {
        return scroll;
    }
    const UiScroll* born = s.ent[scroll].try_get<UiScroll>();
    if (born != nullptr) {
        uiVirtualSyncSpacer(s, born->inner, itemH);
    }
    UiVirtualGrid grid{};
    grid.itemW = itemW;
    grid.itemH = itemH;
    grid.count = cfg.totalItems;
    grid.bind = cfg.onBindCell;
    grid.select = cfg.onSelect;
    grid.activate = cfg.onActivate;
    grid.user = cfg.user;
    s.ent[scroll].set<UiVirtualGrid>(grid);
    s.ent[scroll].set<UiVirtualPool>({uiVirtualGridRefresh, uiGridContentH, 1});
    uiVirtualGridRefresh(s, scroll);
    return scroll;
}

void uiVirtualGridRefresh(UiState& s, uint16_t id) {
    auto* grid = id < s.count ? s.ent[id].try_get_mut<UiVirtualGrid>() : nullptr;
    const UiScroll* scroll = grid != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (grid == nullptr || scroll == nullptr) {
        return;
    }
    const float viewW = s.box[id].w > 32.0f ? s.box[id].w - 16.0f : grid->itemW;
    uint32_t rawCols = grid->itemW > 1.0f ? static_cast<uint32_t>(viewW / grid->itemW) : 1u;
    if (rawCols < 1u) {
        rawCols = 1u;
    }
    if (rawCols > 32u) {
        rawCols = 32u;
    }
    const uint8_t cols = static_cast<uint8_t>(rawCols);
    grid->cols = cols;
    const uint16_t need = uiVirtualSlots(s.box[id].h, grid->itemH, cols);
    if (scroll->inner < s.count) {
        uiVirtualEnsure(s, *grid, need, 4, [&] { return addCell(s, scroll->inner, grid->itemW, grid->itemH); });
    }
    const UiVirtualWindow win = uiVirtualCalc(scroll->offset, s.box[id].h, grid->itemH, grid->count, cols);
    grid->first = win.first;
    uiVirtualSyncSpacer(s, scroll->inner, win.totalHeight + 16.0f);
    for (uint8_t i = 0; i < grid->slots; ++i) {
        const uint16_t slot = grid->slot[i];
        const uint32_t index = win.first + i;
        const bool on = i < win.visibleCount && slot < s.count;
        uiVirtualSetShown(s, slot, on);
        if (!on) {
            continue;
        }
        const uint32_t col = cols > 0 ? index % cols : 0;
        const uint32_t row = cols > 0 ? index / cols : index;
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = 1;
            flex->posX = static_cast<float>(col) * grid->itemW + 8.0f;
            flex->posY = static_cast<float>(row) * grid->itemH + 8.0f;
            flex->widthMode = static_cast<uint8_t>(UiSize::Px);
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->direction = 0;
            flex->align = 1;
            flex->justify = 2;
            flex->width = grid->itemW - 8.0f;
            flex->height = grid->itemH - 8.0f;
            flex->shrink = 0.0f;
        }
        if (const UiParts* parts = s.ent[slot].try_get<UiParts>()) {
            if (auto* icon = parts->icon < s.count ? s.ent[parts->icon].try_get_mut<UiFlex>() : nullptr) {
                const float side = grid->itemW > 72.0f ? 64.0f : 48.0f;
                icon->width = side;
                icon->height = side;
            }
        }
        s.ent[slot].set<UiVirtRow>({index});
        if (grid->bind != nullptr) {
            grid->bind(grid->user, slot, index);
        }
    }
    s.visualDirty = true;
}

} // namespace burnhope

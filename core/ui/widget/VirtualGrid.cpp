#include "ui/widget/Button.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/VirtualGrid.hpp"

#include <cmath>

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
    const float dt = s->time - grid->lastClickTime;
    const float dx = grid->pressX - grid->lastClickX;
    const float dy = grid->pressY - grid->lastClickY;
    const bool dbl = grid->lastClickIndex == index && id == grid->lastClickId && dt >= 0.0f && dt < 0.30f
        && dx * dx + dy * dy < 16.0f;
    grid->lastClickIndex = index;
    grid->lastClickId = id;
    grid->lastClickTime = s->time;
    grid->lastClickX = grid->pressX;
    grid->lastClickY = grid->pressY;
    if (grid->select != nullptr) {
        grid->select(grid->user, index, false);
    }
    if (dbl && grid->activate != nullptr) {
        grid->activate(grid->user, index);
    }
}

uint16_t addCell(UiState& s, uint16_t inner, float itemW, float itemH) {
    const float tile = itemW < itemH ? itemW : itemH;
    UiFlex cell = px(tile > 12.0f ? tile - 4.0f : tile, tile > 12.0f ? tile - 4.0f : tile);
    cell.shrink = 0.0f;
    cell.direction = 0;
    cell.align = 1;
    cell.justify = 1;
    cell.pad = 6.0f;
    cell.gap = 4.0f;
    UiPaint ink = paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm);
    ink.a = 0.0f;
    ink.borderW = 0.0f;
    const uint16_t slot = spawn(s, inner, cell, ink);
    if (slot == kUiNone) {
        return slot;
    }
    UiButtonConfig glyph{};
    const float iconSide = tile > 24.0f ? tile * 0.52f : 20.0f;
    glyph.width = iconSide;
    glyph.height = iconSide;
    const uint16_t icon = uiButton(s, slot, glyph);
    if (icon < s.count) {
        if (auto* paint = s.ent[icon].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
            paint->icon = 4;
        }
    }
    UiFlex lab{};
    lab.self = 1;
    lab.heightMode = static_cast<uint8_t>(UiSize::Px);
    lab.height = 18.0f;
    lab.shrink = 1.0f;
    const uint16_t label = spawn(s, slot, lab, paintToken(s.look, UiRole::Label, UiToken::Text, 0.0f));
    addText(s, label, " ");
    uiTextStyle(s, label, 1, 1, 1, 0, 0, 1.0f);
    s.ent[slot].set<UiParts>({icon, kUiNone, label});
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
    s.ent[scroll].set<UiVirtualPool>({uiVirtualGridRefresh, uiGridContentH, 0});
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
    uint32_t rawCols = 1u;
    if (grid->list == 0 && grid->itemW > 1.0f) {
        rawCols = static_cast<uint32_t>(viewW / grid->itemW);
    }
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
        const uint32_t col = grid->list != 0 || cols == 0 ? 0u : index % cols;
        const uint32_t row = grid->list != 0 || cols == 0 ? index : index / cols;
        const bool rows = grid->list != 0;
        const float square = grid->itemW;
        const float tileW = rows ? (viewW > 24.0f ? viewW : grid->itemW) : square;
        const float tileH = rows ? grid->itemH - 4.0f : square;
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = 1;
            flex->posX = rows ? 4.0f : static_cast<float>(col) * grid->itemW + 8.0f;
            flex->posY = static_cast<float>(row) * grid->itemH - scroll->offset + (rows ? 2.0f : 8.0f);
            flex->widthMode = static_cast<uint8_t>(UiSize::Px);
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->direction = rows ? 1 : 0;
            flex->align = 1;
            flex->justify = rows ? 0 : 1;
            flex->gap = rows ? 6.0f : 4.0f;
            flex->pad = rows ? 8.0f : 6.0f;
            flex->width = tileW > 8.0f ? tileW : 8.0f;
            flex->height = tileH > 8.0f ? tileH : 8.0f;
            flex->shrink = 0.0f;
        }
        if (const UiParts* parts = s.ent[slot].try_get<UiParts>()) {
            if (auto* icon = parts->icon < s.count ? s.ent[parts->icon].try_get_mut<UiFlex>() : nullptr) {
                const float side = rows ? 22.0f : (square > 24.0f ? square * 0.52f : 20.0f);
                icon->width = side;
                icon->height = side;
                icon->shrink = 0.0f;
            }
            if (parts->label < s.count) {
                if (auto* lab = s.ent[parts->label].try_get_mut<UiFlex>()) {
                    lab->self = 1;
                    lab->heightMode = static_cast<uint8_t>(UiSize::Px);
                    lab->height = 18.0f;
                    lab->grow = rows ? 1.0f : 0.0f;
                    lab->shrink = 1.0f;
                }
                uiTextStyle(s, parts->label, rows ? 0 : 1, 1, 1, 0, 0, 1.0f);
            }
        }
        s.ent[slot].set<UiVirtRow>({index});
        if (grid->bind != nullptr) {
            grid->bind(grid->user, slot, index);
        }
    }
    s.visualDirty = true;
}

void uiVirtualGridClearClick(UiState& s, uint16_t gridId) {
    auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
    if (grid == nullptr) {
        return;
    }
    grid->lastClickIndex = 0xFFFFFFFFu;
    grid->lastClickId = kUiNone;
    grid->lastClickTime = 0.0f;
    grid->lastClickX = 0.0f;
    grid->lastClickY = 0.0f;
}

bool uiVirtualGridInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    (void)cursor;
    if (phase != kUiInPress) {
        return false;
    }
    const uint16_t gridId = id < s.count && s.ent[id].try_get<UiVirtualGrid>() != nullptr
        ? id
        : uiAncestorId<UiVirtualGrid>(s, id);
    if (gridId == kUiNone) {
        return false;
    }
    auto* grid = s.ent[gridId].try_get_mut<UiVirtualGrid>();
    if (grid == nullptr) {
        return false;
    }
    grid->pressX = in.pointer.x;
    grid->pressY = in.pointer.y;
    return false;
}

} // namespace burnhope

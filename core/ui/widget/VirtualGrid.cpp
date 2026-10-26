#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/State.hpp"

#include <cstring>

#include <cmath>

namespace burnhope {
namespace {

void onCell(void* user, uint16_t id) {
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
    cell.direction = UiFlexDir::Column;
    cell.align = UiAlign::Center;
    cell.justify = UiJustify::Center;
    cell.pad = s.metrics.cellPad;
    cell.gap = s.metrics.cellGap;
    UiPaint ink = paintToken(s.look, UiToken::Item, s.look.radiusSm);
    ink.a = 0.0f;
    ink.borderW = 0.0f;
    const uint16_t slot = spawn(s, inner, cell, ink);
    if (slot == kUiNone) {
        return slot;
    }
    const float iconSide = tile > 24.0f ? tile * 0.52f : 20.0f;
    UiFlex glyph{};
    glyph.widthMode = UiSize::Px;
    glyph.width = iconSide;
    glyph.heightMode = UiSize::Px;
    glyph.height = iconSide;
    glyph.shrink = 0.0f;
    UiPaint glyphPaint{};
    glyphPaint.a = 0.0f;
    glyphPaint.borderW = 0.0f;
    glyphPaint.shadow = 0.0f;
    glyphPaint.icon = static_cast<uint8_t>(UiIcon::Folder);
    glyphPaint.r = s.look.text[0];
    glyphPaint.g = s.look.text[1];
    glyphPaint.b = s.look.text[2];
    const uint16_t icon = spawn(s, slot, glyph, glyphPaint);
    UiFlex lab{};
    lab.self = UiAlignSelf::Stretch;
    lab.heightMode = UiSize::Px;
    lab.height = s.metrics.labelH;
    lab.shrink = 1.0f;
    const uint16_t label = spawn(s, slot, lab, paintLabel(s.look));
    addText(s, label, " ");
    uiTextStyle(s, label, UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (icon < s.count) {
        s.ent[icon].add<UiNoHit>();
    }
    if (label < s.count) {
        s.ent[label].add<UiNoHit>();
    }
    s.ent[slot].add<UiFocusable>();
    uiHover(s, slot);
    s.ent[slot].set<UiParts>({icon, kUiNone, label});
    uiBind(s, slot, onCell, &s);
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

static float uiGridContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight(s, id, id < s.count ? s.ent[id].try_get<UiVirtualGrid>() : nullptr);
}

void gridOnLayout(UiState& s, uint16_t id, void*) {
    uiVirtualGridRefresh(s, id);
}

uint16_t uiVirtualGrid(UiState& s, uint16_t parent, const UiVirtualGridConfig& cfg) {
    const float itemW = cfg.itemW < 32.0f ? 84.0f : cfg.itemW;
    const float itemH = cfg.itemH < 32.0f ? 96.0f : cfg.itemH;
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiToken::Content, s.look.radiusSm);
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
    grid.role = cfg.onRole;
    grid.hover = cfg.onHover;
    grid.context = cfg.onContext;
    grid.empty = cfg.onEmptyClick;
    grid.zoom = cfg.onZoom;
    grid.commit = cfg.onCommit;
    grid.cancel = cfg.onCancel;
    grid.openEditor = cfg.onOpenEditor;
    grid.readEditor = cfg.onReadEditor;
    grid.user = cfg.user;
    s.ent[scroll].set<UiVirtualGrid>(grid);
    uiVirtualAttach(s, scroll, uiVirtualGridRefresh, uiGridContentH, 0);
    s.ent[scroll].set<UiLayoutRefresh>({gridOnLayout, nullptr});
    uiVirtualGridRefresh(s, scroll);
    return scroll;
}

void uiBindGrid(UiState& s, const char* name, const UiVirtualGridConfig& cfg) {
    const uint16_t id = uiFindName(s, name);
    auto* grid = id < s.count ? s.ent[id].try_get_mut<UiVirtualGrid>() : nullptr;
    if (grid == nullptr) {
        return;
    }
    const float itemW = cfg.itemW < 32.0f ? 84.0f : cfg.itemW;
    const float itemH = cfg.itemH < 32.0f ? 96.0f : cfg.itemH;
    grid->itemW = itemW;
    grid->itemH = itemH;
    grid->count = cfg.totalItems;
    grid->bind = cfg.onBindCell;
    grid->select = cfg.onSelect;
    grid->activate = cfg.onActivate;
    grid->role = cfg.onRole;
    grid->hover = cfg.onHover;
    grid->context = cfg.onContext;
    grid->empty = cfg.onEmptyClick;
    grid->zoom = cfg.onZoom;
    grid->commit = cfg.onCommit;
    grid->cancel = cfg.onCancel;
    grid->openEditor = cfg.onOpenEditor;
    grid->readEditor = cfg.onReadEditor;
    grid->user = cfg.user;
    uiVirtualGridRefresh(s, id);
}

void uiVirtualGridSetView(UiState& s, uint16_t id, uint32_t count, uint8_t list, float itemW, float itemH) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    auto* grid = s.ent[id].try_get_mut<UiVirtualGrid>();
    if (grid == nullptr) {
        return;
    }
    grid->count = count;
    grid->list = list;
    grid->itemW = itemW;
    grid->itemH = itemH;
    uiVirtualGridRefresh(s, id);
}

void uiVirtualGridRefresh(UiState& s, uint16_t id) {
    auto* grid = id < s.count ? s.ent[id].try_get_mut<UiVirtualGrid>() : nullptr;
    const UiScroll* scroll = grid != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (grid == nullptr || scroll == nullptr) {
        return;
    }
    const float viewW = s.box[id].w > 32.0f ? s.box[id].w - s.metrics.gridInset * 2.0f : grid->itemW;
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
    uiVirtualSyncSpacer(s, scroll->inner, win.totalHeight + s.metrics.gridInset * 2.0f);
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
            flex->position = UiPosition::Absolute;
            flex->posX = rows ? s.metrics.cellGap : static_cast<float>(col) * grid->itemW + s.metrics.gridInset;
            flex->posY = uiVirtualSlotY(row, grid->itemH, scroll->offset) + (rows ? 2.0f : s.metrics.gridInset);
            flex->widthMode = UiSize::Px;
            flex->heightMode = UiSize::Px;
            flex->direction = rows ? UiFlexDir::Row : UiFlexDir::Column;
            flex->align = UiAlign::Center;
            flex->justify = rows ? UiJustify::Start : UiJustify::Center;
            flex->gap = rows ? s.metrics.listGap : s.metrics.cellGap;
            flex->pad = rows ? s.metrics.listPad : s.metrics.cellPad;
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
                    lab->self = UiAlignSelf::Stretch;
                    lab->heightMode = UiSize::Px;
                    lab->height = s.metrics.labelH;
                    lab->grow = rows ? 1.0f : 0.0f;
                    lab->shrink = 1.0f;
                }
                uiTextStyle(s, parts->label, rows ? UiTextAlign::Left : UiTextAlign::Center, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
            }
        }
        s.ent[slot].set<UiVirtRow>({index});
        if (grid->bind != nullptr) {
            grid->bind(grid->user, slot, index);
        }
        if (grid->role != nullptr) {
            if (auto* paint = s.ent[slot].try_get_mut<UiPaint>()) {
                const uint8_t role = grid->role(grid->user, index);
                if (role == kUiCellSelected) {
                    *paint = paintToken(s.look, UiToken::Accent, s.look.radiusSm);
                    paint->a = s.metrics.selectedA;
                    paint->borderW = 0.0f;
                } else if (role == kUiCellHeader) {
                    *paint = paintToken(s.look, UiToken::Side, 0.0f);
                    paint->a = s.metrics.headerA;
                    paint->borderW = 0.0f;
                } else {
                    *paint = paintToken(s.look, UiToken::Item, s.look.radiusSm);
                    paint->a = 0.0f;
                    paint->borderW = 0.0f;
                }
            }
        }
        if (grid->editAt == index) {
            if (const UiParts* parts = s.ent[slot].try_get<UiParts>(); parts != nullptr && parts->label < s.count) {
                uiText(s, parts->label, "");
            }
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

namespace {

uint16_t slotFor(const UiState& s, const UiVirtualGrid& grid, uint32_t index) {
    for (uint8_t i = 0; i < grid.slots; ++i) {
        const uint16_t slot = grid.slot[i];
        const UiVirtRow* row = slot < s.count ? s.ent[slot].try_get<UiVirtRow>() : nullptr;
        if (row != nullptr && row->index == index && nodeShown(s, slot)) {
            return slot;
        }
    }
    return kUiNone;
}

void finishEdit(UiState& s, uint16_t gridId, bool commit) {
    auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
    if (grid == nullptr || grid->editAt == kUiCellNone) {
        return;
    }
    const uint32_t index = grid->editAt;
    const uint16_t editor = grid->editor;
    grid->editAt = kUiCellNone;
    char text[96]{};
    if (editor < s.count) {
        if (grid->readEditor != nullptr) {
            grid->readEditor(grid->user, editor, text, 96);
        }
        uiShow(s, editor, false);
    }
    if (commit && grid->commit != nullptr) {
        grid->commit(grid->user, index, text);
    } else if (!commit && grid->cancel != nullptr) {
        grid->cancel(grid->user, index);
    }
}

bool placeEditor(UiState& s, uint16_t gridId) {
    auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
    if (grid == nullptr || grid->editAt == kUiCellNone || grid->editor >= s.count) {
        return false;
    }
    const uint16_t slot = slotFor(s, *grid, grid->editAt);
    return uiPlaceOverSlot(s, grid->editor, slot, s.metrics.searchH);
}

} // namespace

void uiVirtualGridBeginEdit(UiState& s, uint16_t gridId, uint32_t index, const char* text) {
    auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
    if (grid == nullptr || index >= grid->count) {
        return;
    }
    if (grid->openEditor == nullptr) {
        return;
    }
    const uint16_t editor = grid->openEditor(grid->user, grid->editor, index, text);
    if (editor == kUiNone || editor >= s.count) {
        return;
    }
    grid->editor = editor;
    grid->editAt = index;
    (void)placeEditor(s, gridId);
}

bool uiVirtualGridEditing(const UiState& s, uint16_t gridId) {
    const UiVirtualGrid* grid = gridId < s.count ? s.ent[gridId].try_get<UiVirtualGrid>() : nullptr;
    return grid != nullptr && grid->editAt != kUiCellNone && s.focused == grid->editor;
}

bool uiVirtualGridInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote&) {
    if (phase == kUiInEscape) {
        uint16_t grids[kUiCap];
        const uint16_t count = uiCollect<UiVirtualGrid>(s, grids, kUiCap);
        for (uint16_t g = 0; g < count; ++g) {
            if (s.ent[grids[g]].try_get<UiVirtualGrid>()->editAt != kUiCellNone) {
                finishEdit(s, grids[g], false);
            }
        }
        return false;
    }
    if (phase == kUiInKey && in.enter != 0) {
        uint16_t grids[kUiCap];
        const uint16_t count = uiCollect<UiVirtualGrid>(s, grids, kUiCap);
        for (uint16_t g = 0; g < count; ++g) {
            const uint16_t n = grids[g];
            const UiVirtualGrid* grid = s.ent[n].try_get<UiVirtualGrid>();
            if (grid != nullptr && grid->editAt != kUiCellNone && s.focused == grid->editor) {
                finishEdit(s, n, true);
                return true;
            }
        }
        return false;
    }
    if (phase == kUiInWheel && in.ctrl != 0) {
        const uint16_t gridId = id < s.count && s.ent[id].try_get<UiVirtualGrid>() != nullptr
            ? id
            : uiAncestorId<UiVirtualGrid>(s, id);
        auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
        if (grid != nullptr && grid->zoom != nullptr) {
            grid->zoom(grid->user, in.wheel);
            return true;
        }
        return false;
    }
    if (phase == kUiInPress) {
        const uint16_t gridId = id < s.count && s.ent[id].try_get<UiVirtualGrid>() != nullptr
            ? id
            : uiAncestorId<UiVirtualGrid>(s, id);
        auto* grid = gridId < s.count ? s.ent[gridId].try_get_mut<UiVirtualGrid>() : nullptr;
        if (grid == nullptr) {
            return false;
        }
        grid->pressX = in.pointer.x;
        grid->pressY = in.pointer.y;
        if ((in.pointer.pressed & kPointerLeft) != 0 && id == gridId && grid->empty != nullptr) {
            grid->empty(grid->user);
        }
        return false;
    }
    if (phase != kUiInSync) {
        return false;
    }
    uint16_t grids[kUiCap];
    const uint16_t gridCount = uiCollect<UiVirtualGrid>(s, grids, kUiCap);
    for (uint16_t g = 0; g < gridCount; ++g) {
        const uint16_t n = grids[g];
        auto* grid = s.ent[n].try_get_mut<UiVirtualGrid>();
        if (grid == nullptr) {
            continue;
        }
        if (grid->editAt != kUiCellNone) {
            placeEditor(s, n);
            if (s.focused != grid->editor && nodeShown(s, grid->editor)) {
                finishEdit(s, n, true);
            }
        }
        const bool inside = s.hovered == n || uiWithin(s, s.hovered, n);
        uint32_t index = kUiCellNone;
        if (inside) {
            (void)uiVirtualHitRow(s, n, index);
        }
        if (index != grid->hoverAt && grid->hover != nullptr) {
            grid->hoverAt = index;
            grid->hover(grid->user, index);
        } else {
            grid->hoverAt = index;
        }
        if (inside && (in.pointer.pressed & kPointerRight) != 0 && grid->context != nullptr) {
            grid->context(grid->user, index, in.pointer.x, in.pointer.y);
        }
    }
    return false;
}

uint16_t loadGrid(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char*, const char*, const char*, void*) {
    const float itemW = node.width > 1.0f ? node.width : 84.0f;
    const float itemH = node.height > 1.0f ? node.height : itemW;
    return uiVirtualGrid(s, parent, {.itemW = itemW, .itemH = itemH});
}

void uiGridBin(UiState& s) {
    uiBinRole(s, kBinSlotGrid, "grid", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotGrid, loadGrid, nullptr);
}

} // namespace burnhope

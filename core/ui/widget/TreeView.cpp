#include "ui/widget/Detail.hpp"

namespace burnhope {
namespace {

void onRow(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    UiTreeView* tree = uiAncestor<UiTreeView>(*s, id);
    uint16_t rowId = s->ent[id].try_get<UiVirtRow>() != nullptr ? id : uiAncestorId<UiVirtRow>(*s, id);
    const UiVirtRow* row = rowId < s->count ? s->ent[rowId].try_get<UiVirtRow>() : nullptr;
    if (tree == nullptr || row == nullptr || row->index >= tree->count) {
        return;
    }
    const UiParts* parts = s->ent[rowId].try_get<UiParts>();
    const bool fold = parts != nullptr && id == parts->chevron;
    if (fold && tree->toggle != nullptr) {
        tree->toggle(tree->user, row->index);
        return;
    }
    if (tree->select != nullptr) {
        tree->select(tree->user, row->index);
    }
}

uint16_t addTreeSlot(UiState& s, uint16_t inner, float rowH) {
    UiFlex row{};
    row.position = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Percent);
    row.width = 100.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = rowH > 2.0f ? rowH - 2.0f : rowH;
    row.shrink = 0.0f;
    row.direction = 1;
    row.align = 1;
    row.gap = 4.0f;
    const uint16_t slot = spawn(s, inner, row, paintToken(s.look, UiRole::Panel, UiToken::Item, s.look.radiusSm));
    if (slot == kUiNone) {
        return slot;
    }
    UiButtonConfig chev{};
    chev.label = ">";
    chev.width = 16.0f;
    chev.height = rowH > 4.0f ? rowH - 4.0f : rowH;
    chev.onClick = onRow;
    chev.user = &s;
    const uint16_t chevron = uiButton(s, slot, chev);
    const uint16_t icon = uiIcon(s, slot, 0, 16.0f, 16.0f);
    UiButtonConfig caption{};
    caption.label = " ";
    caption.width = -1.0f;
    caption.height = rowH > 4.0f ? rowH - 4.0f : rowH;
    caption.onClick = onRow;
    caption.user = &s;
    const uint16_t label = uiButton(s, slot, caption);
    s.ent[slot].set<UiParts>({icon, chevron, label});
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

uint16_t uiTreeView(UiState& s, uint16_t parent, const UiTreeConfig& cfg) {
    UiFlex host{};
    host.grow = 1.0f;
    host.shrink = 1.0f;
    host.overflow = 2;
    host.position = 2;
    const uint16_t scroll = spawn(s, parent, host, paintToken(s.look, UiRole::Scroll, UiToken::Side, s.look.radiusSm));
    UiFlex spacer{};
    spacer.widthMode = static_cast<uint8_t>(UiSize::Percent);
    spacer.width = 100.0f;
    spacer.heightMode = static_cast<uint8_t>(UiSize::Px);
    spacer.height = 28.0f;
    spacer.shrink = 0.0f;
    spacer.position = 2;
    const uint16_t inner = spawn(s, scroll, spacer, paint(UiRole::Hidden, 0, 0, 0));
    s.ent[scroll].set<UiScroll>({0.0f, inner});
    UiTreeView tree{};
    tree.count = cfg.totalNodes;
    tree.bind = cfg.onBindNode;
    tree.toggle = cfg.onToggleNode;
    tree.select = cfg.onSelectNode;
    tree.user = cfg.user;
    tree.rowH = 28.0f;
    s.ent[scroll].set<UiTreeView>(tree);
    s.ent[scroll].set<UiVirtualPool>({uiTreeViewRefresh});
    uiTreeViewRefresh(s, scroll);
    return scroll;
}

void uiTreeViewRefresh(UiState& s, uint16_t id) {
    auto* tree = id < s.count ? s.ent[id].try_get_mut<UiTreeView>() : nullptr;
    const UiScroll* scroll = tree != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (tree == nullptr || scroll == nullptr) {
        return;
    }
    const uint16_t need = uiVirtualSlots(s.box[id].h, tree->rowH, 1);
    while (tree->slots < need && tree->slots < kUiPoolCap && scroll->inner < s.count && s.count + 8 < kUiCap) {
        const uint16_t slot = addTreeSlot(s, scroll->inner, tree->rowH);
        if (slot == kUiNone) {
            break;
        }
        tree->slot[tree->slots++] = slot;
    }
    const UiVirtualWindow win = uiVirtualCalc(scroll->offset, s.box[id].h, tree->rowH, tree->count, 1);
    tree->first = win.first;
    if (scroll->inner < s.count) {
        if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
            flex->height = win.totalHeight;
        }
    }
    for (uint8_t i = 0; i < tree->slots; ++i) {
        const uint16_t slot = tree->slot[i];
        const uint32_t index = win.first + i;
        const bool on = i < win.visibleCount && slot < s.count;
        const bool live = slot < s.count && s.yoga[slot] != nullptr && YGNodeStyleGetDisplay(s.yoga[slot]) != YGDisplayNone;
        if (live != on) {
            uiShow(s, slot, on);
        }
        if (!on) {
            continue;
        }
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = 1;
            flex->posY = static_cast<float>(index) * tree->rowH;
            flex->height = tree->rowH > 2.0f ? tree->rowH - 2.0f : tree->rowH;
        }
        s.ent[slot].set<UiVirtRow>({index});
        if (tree->bind != nullptr) {
            tree->bind(tree->user, slot, index);
        }
    }
    s.visualDirty = true;
}

void uiRowIndent(UiState& s, uint16_t id, float pad) {
    if (auto* flex = id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr) {
        flex->padL = pad;
        uiMarkLayout(s, id);
    }
}

} // namespace burnhope

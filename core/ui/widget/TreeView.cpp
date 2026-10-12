#include "ui/widget/Button.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"

namespace burnhope {
namespace {

void onRow(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    UiTreeView* tree = uiAncestor<UiTreeView>(*s, id);
    const uint32_t index = uiVirtRowIndex(*s, id);
    if (tree == nullptr || index >= tree->count) {
        return;
    }
    const uint16_t rowId = s->ent[id].try_get<UiVirtRow>() != nullptr ? id : uiAncestorId<UiVirtRow>(*s, id);
    const UiParts* parts = rowId < s->count ? s->ent[rowId].try_get<UiParts>() : nullptr;
    const bool fold = parts != nullptr && id == parts->chevron;
    if (fold && tree->toggle != nullptr) {
        tree->toggle(tree->user, index);
        return;
    }
    if (tree->select != nullptr) {
        tree->select(tree->user, index);
    }
}

uint16_t addTreeSlot(UiState& s, uint16_t inner, float itemH) {
    UiFlex row{};
    row.position = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Percent);
    row.width = 100.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = itemH > 2.0f ? itemH - 2.0f : itemH;
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
    chev.height = itemH > 4.0f ? itemH - 4.0f : itemH;
    chev.onClick = onRow;
    chev.user = &s;
    const uint16_t chevron = uiButton(s, slot, chev);
    const uint16_t icon = uiIcon(s, slot, 0, 16.0f, 16.0f);
    UiButtonConfig caption{};
    caption.label = " ";
    caption.width = -1.0f;
    caption.height = itemH > 4.0f ? itemH - 4.0f : itemH;
    caption.onClick = onRow;
    caption.user = &s;
    const uint16_t label = uiButton(s, slot, caption);
    s.ent[slot].set<UiParts>({icon, chevron, label});
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

float uiTreeContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight<UiTreeView>(s, id);
}

uint16_t uiTreeView(UiState& s, uint16_t parent, const UiTreeConfig& cfg) {
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiRole::Scroll, UiToken::Side, s.look.radiusSm);
    if (scroll == kUiNone) {
        return scroll;
    }
    const UiScroll* born = s.ent[scroll].try_get<UiScroll>();
    if (born != nullptr) {
        uiVirtualSyncSpacer(s, born->inner, 28.0f);
    }
    UiTreeView tree{};
    tree.count = cfg.totalNodes;
    tree.bind = cfg.onBindNode;
    tree.toggle = cfg.onToggleNode;
    tree.select = cfg.onSelectNode;
    tree.user = cfg.user;
    tree.itemH = 28.0f;
    s.ent[scroll].set<UiTreeView>(tree);
    s.ent[scroll].set<UiVirtualPool>({uiTreeViewRefresh, uiTreeContentH, 1});
    uiTreeViewRefresh(s, scroll);
    return scroll;
}

void uiTreeViewRefresh(UiState& s, uint16_t id) {
    auto* tree = id < s.count ? s.ent[id].try_get_mut<UiTreeView>() : nullptr;
    const UiScroll* scroll = tree != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (tree == nullptr || scroll == nullptr) {
        return;
    }
    const uint16_t need = uiVirtualSlots(s.box[id].h, tree->itemH, 1);
    if (scroll->inner < s.count) {
        uiVirtualEnsure(s, *tree, need, 8, [&] { return addTreeSlot(s, scroll->inner, tree->itemH); });
    }
    const UiVirtualWindow win = uiVirtualCalc(scroll->offset, s.box[id].h, tree->itemH, tree->count, 1);
    tree->first = win.first;
    uiVirtualSyncSpacer(s, scroll->inner, win.totalHeight);
    for (uint8_t i = 0; i < tree->slots; ++i) {
        const uint16_t slot = tree->slot[i];
        const uint32_t index = win.first + i;
        const bool on = i < win.visibleCount && slot < s.count;
        uiVirtualSetShown(s, slot, on);
        if (!on) {
            continue;
        }
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = 1;
            flex->posY = static_cast<float>(index) * tree->itemH;
            flex->height = tree->itemH > 2.0f ? tree->itemH - 2.0f : tree->itemH;
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

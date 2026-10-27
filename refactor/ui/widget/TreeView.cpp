#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/Panel.hpp"

namespace burnhope {
namespace {

void onRow(void* user, uint16_t id) {
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
    const UiTemplateId tpl = uiTemplateFind(s, "core/tree_row");
    const uint16_t slot = uiSpawnTemplate(s, inner, tpl);
    if (slot == kUiNone) {
        return slot;
    }
    const uint16_t chevron = uiTemplateSlot(s, tpl, slot, "chevron");
    const uint16_t icon = uiTemplateSlot(s, tpl, slot, "icon");
    const uint16_t label = uiTemplateSlot(s, tpl, slot, "label");
    // The row hangs in the virtual list: absolute, full width, a little shorter than its pitch.
    uiPlace(s, slot, true, 0.0f, 0.0f, 0.0f, itemH > 2.0f ? itemH - 2.0f : itemH);
    uiSetWidthPercent(s, slot, 100.0f);
    const float rowH = itemH > 6.0f ? itemH - 2.0f : itemH;
    if (auto* box = s.ent[chevron].try_get_mut<UiFlex>()) {
        box->heightMode = UiSize::Px;
        box->height = rowH;
    }
    uiSetIcon(s, chevron, UiIcon::ChevronRight);
    uiBind(s, chevron, onRow, &s);
    uiSetIcon(s, icon, UiIcon::Folder);
    if (auto* caption = s.ent[label].try_get_mut<UiFlex>()) {
        caption->heightMode = UiSize::Px;
        caption->height = itemH > 6.0f ? itemH - 6.0f : itemH;
    }
    uiText(s, label, " ");
    uiBind(s, slot, onRow, &s);
    s.ent[slot].set<UiParts>({icon, chevron, label});
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

static float uiTreeContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight(s, id, id < s.count ? s.ent[id].try_get<UiTreeView>() : nullptr);
}

void treeOnLayout(UiState& s, uint16_t id, void*) {
    uiTreeViewRefresh(s, id);
}

uint16_t uiTreeView(UiState& s, uint16_t parent, const UiTreeConfig& cfg) {
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiToken::Side, s.look.radiusSm);
    if (scroll == kUiNone) {
        return scroll;
    }
    const UiScroll* born = s.ent[scroll].try_get<UiScroll>();
    if (born != nullptr) {
        uiVirtualSyncSpacer(s, born->inner, uiStyle(s, "tree", UiStyleKey::RowH));
    }
    UiTreeView tree{};
    tree.count = cfg.totalNodes;
    tree.bind = cfg.onBindNode;
    tree.toggle = cfg.onToggleNode;
    tree.select = cfg.onSelectNode;
    tree.depth = cfg.onDepth;
    tree.open = cfg.onOpen;
    tree.empty = cfg.onEmptyClick;
    tree.user = cfg.user;
    tree.itemH = uiStyle(s, "tree", UiStyleKey::RowH);
    s.ent[scroll].set<UiTreeView>(tree);
    uiVirtualAttach(s, scroll, uiTreeViewRefresh, uiTreeContentH, 0);
    s.ent[scroll].set<UiLayoutRefresh>({treeOnLayout, nullptr});
    uiTreeViewRefresh(s, scroll);
    return scroll;
}

void uiBindTree(UiState& s, const char* name, const UiTreeConfig& cfg) {
    const uint16_t id = uiFindName(s, name);
    auto* tree = id < s.count ? s.ent[id].try_get_mut<UiTreeView>() : nullptr;
    if (tree == nullptr) {
        return;
    }
    tree->count = cfg.totalNodes;
    tree->bind = cfg.onBindNode;
    tree->toggle = cfg.onToggleNode;
    tree->select = cfg.onSelectNode;
    tree->depth = cfg.onDepth;
    tree->open = cfg.onOpen;
    tree->empty = cfg.onEmptyClick;
    tree->user = cfg.user;
    uiTreeViewRefresh(s, id);
}

void uiTreeViewSetCount(UiState& s, uint16_t id, uint32_t count) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    auto* tree = s.ent[id].try_get_mut<UiTreeView>();
    if (tree == nullptr) {
        return;
    }
    tree->count = count;
    uiTreeViewRefresh(s, id);
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
            flex->position = UiPosition::Absolute;
            flex->posX = 0.0f;
            flex->posY = uiVirtualSlotY(index, tree->itemH, scroll->offset);
            flex->widthMode = UiSize::Percent;
            flex->width = 100.0f;
            flex->justify = UiJustify::Start;
            flex->align = UiAlign::Center;
            flex->direction = UiFlexDir::Row;
            flex->height = tree->itemH > 2.0f ? tree->itemH - 2.0f : tree->itemH;
        }
        s.ent[slot].set<UiVirtRow>({index});
        if (tree->depth != nullptr) {
            uiRowIndent(s, slot, static_cast<float>(tree->depth(tree->user, index)) * uiStyle(s, "tree", UiStyleKey::TreeIndent));
        }
        if (const UiParts* parts = s.ent[slot].try_get<UiParts>(); parts != nullptr && parts->chevron < s.count && tree->open != nullptr) {
            uiShow(s, parts->chevron, true);
            if (auto* paint = s.ent[parts->chevron].try_get_mut<UiPaint>()) {
                const bool opened = tree->open(tree->user, index) != 0;
                paint->icon = static_cast<uint8_t>(opened ? UiIcon::ChevronDown : UiIcon::ChevronRight);
            }
        }
        if (tree->bind != nullptr) {
            tree->bind(tree->user, slot, index);
        }
    }
    s.visualDirty = true;
}

bool uiTreeInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote&) {
    if (phase != kUiInPress || (in.pointer.pressed & kPointerLeft) == 0) {
        return false;
    }
    if (id >= s.count) {
        return false;
    }
    auto* tree = s.ent[id].try_get<UiTreeView>();
    if (tree == nullptr || tree->empty == nullptr) {
        return false;
    }
    tree->empty(tree->user);
    return false;
}

uint16_t loadTree(UiState& s, uint16_t parent, const BinNode&, const BinTail&, const char*, const char*, const char*, void*) {
    return uiTreeView(s, parent, {});
}

void uiTreeBin(UiState& s) {
    uiBinRole(s, kBinSlotTree, "tree", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotTree, loadTree, nullptr);
}

} // namespace burnhope

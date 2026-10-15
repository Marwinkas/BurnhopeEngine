#include "ui/Hover.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"

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
    UiFlex row{};
    row.position = UiPosition::Absolute;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    row.heightMode = UiSize::Px;
    row.height = itemH > 2.0f ? itemH - 2.0f : itemH;
    row.shrink = 0.0f;
    row.direction = UiFlexDir::Row;
    row.align = UiAlign::Center;
    row.justify = UiJustify::Start;
    row.gap = 4.0f;
    UiPaint rowPaint = paintToken(s.look, UiToken::Item, s.look.radiusSm);
    rowPaint.a = 0.0f;
    rowPaint.borderW = 0.0f;
    const uint16_t slot = spawn(s, inner, row, rowPaint);
    if (slot == kUiNone) {
        return slot;
    }
    auto bareIcon = [&](float side, uint8_t iconId) {
        UiFlex box{};
        box.widthMode = UiSize::Px;
        box.width = side;
        box.heightMode = UiSize::Px;
        box.height = side;
        box.shrink = 0.0f;
        UiPaint paint{};
        paint.a = 0.0f;
        paint.borderW = 0.0f;
        paint.shadow = 0.0f;
        paint.icon = iconId;
        paint.r = s.look.text[0];
        paint.g = s.look.text[1];
        paint.b = s.look.text[2];
        return spawn(s, slot, box, paint);
    };
    const uint16_t chevron = bareIcon(16.0f, 10);
    if (chevron < s.count) {
        s.ent[chevron].add<UiFocusable>();
        uiBind(s, chevron, onRow, &s);
    }
    const uint16_t icon = bareIcon(16.0f, 4);
    UiFlex caption{};
    caption.grow = 1.0f;
    caption.shrink = 1.0f;
    caption.self = UiAlignSelf::Stretch;
    caption.heightMode = UiSize::Px;
    caption.height = itemH > 6.0f ? itemH - 6.0f : itemH;
    const uint16_t label = spawn(s, slot, caption, paintLabel(s.look));
    addText(s, label, " ");
    uiTextStyle(s, label, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (icon < s.count) {
        s.ent[icon].add<UiNoHit>();
    }
    if (label < s.count) {
        s.ent[label].add<UiNoHit>();
    }
    s.ent[slot].add<UiFocusable>();
    uiHover(s, slot);
    uiBind(s, slot, onRow, &s);
    s.ent[slot].set<UiParts>({icon, chevron, label});
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

float uiTreeContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight<UiTreeView>(s, id);
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
    s.ent[scroll].set<UiVirtualPool>({uiTreeViewRefresh, uiTreeContentH, 0});
    s.ent[scroll].set<UiLayoutRefresh>({treeOnLayout, nullptr});
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
            flex->position = UiPosition::Absolute;
            flex->posX = 0.0f;
            flex->posY = static_cast<float>(index) * tree->itemH - scroll->offset;
            flex->widthMode = UiSize::Percent;
            flex->width = 100.0f;
            flex->justify = UiJustify::Start;
            flex->align = UiAlign::Center;
            flex->direction = UiFlexDir::Row;
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

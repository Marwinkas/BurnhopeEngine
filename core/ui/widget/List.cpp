#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {
namespace {

void onTree(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* tree = s->ent[id].try_get_mut<UiTree>();
    if (tree == nullptr || tree->body == kUiNone) {
        return;
    }
    tree->open = tree->open == 0 ? 1 : 0;
    uiShow(*s, tree->body, tree->open != 0);
}

} // namespace

uint16_t listItemOf(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        if (s.parentOf[p] == s.listInner) {
            return p;
        }
    }
    return kUiNone;
}

void moveListItem(UiState& s, uint16_t item, uint16_t before, bool after) {
    YGNodeRef parent = s.yoga[s.listInner];
    YGNodeRef node = s.yoga[item];
    if (parent == nullptr || node == nullptr) {
        return;
    }
    const uint32_t count = YGNodeGetChildCount(parent);
    uint32_t from = count;
    uint32_t to = count;
    for (uint32_t i = 0; i < count; ++i) {
        if (YGNodeGetChild(parent, i) == node) {
            from = i;
        }
        if (before != kUiNone && YGNodeGetChild(parent, i) == s.yoga[before]) {
            to = i;
        }
    }
    if (from == count) {
        return;
    }
    if (after && to < count) {
        ++to;
    }
    if (from < to) {
        --to;
    }
    if (from == to) {
        return;
    }
    YGNodeRemoveChild(parent, node);
    YGNodeInsertChild(parent, node, to);
    s.layoutDirty = true;
}

uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text) {
    UiFlex row{};
    row.direction = 1;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = 1;
    row.gap = 6.0f;
    const uint16_t head = spawn(s, parent, row, paint(UiRole::Button, 0.14f, 0.15f, 0.18f, 4.0f));
    addText(s, head, text != nullptr ? text : "");
    UiFlex body{};
    body.direction = 0;
    body.padL = 16.0f;
    body.shrink = 0.0f;
    const uint16_t child = spawn(s, parent, body, paint(UiRole::Hidden, 0, 0, 0));
    uiShow(s, child, false);
    s.ent[head].set<UiTree>({child, 0});
    uiBind(s, head, onTree, &s);
    return child;
}

} // namespace burnhope

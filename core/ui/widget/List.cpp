#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

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

} // namespace burnhope

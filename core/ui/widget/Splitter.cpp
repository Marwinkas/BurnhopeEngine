#include "ui/widget/Detail.hpp"
#include "ui/widget/Splitter.hpp"

namespace burnhope {
namespace {

uint16_t childAt(const UiState& s, uint16_t parent, uint32_t index) {
    return uiNodeChild(s, parent, index);
}

int childIndex(const UiState& s, uint16_t id) {
    const uint16_t parent = s.parentOf[id];
    const uint32_t count = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < count; ++i) {
        if (uiNodeChild(s, parent, i) == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool sized(const UiFlex& flex, bool vertical) {
    const uint8_t mode = vertical ? flex.heightMode : flex.widthMode;
    return mode == static_cast<uint8_t>(UiSize::Px) && flex.grow < 0.5f;
}

float axisFloor(const UiState& s, uint16_t id, bool vertical, bool ignoreFixed, int depth) {
    if (!nodeShown(s, id) || depth > 6) {
        return 0.0f;
    }
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    if (flex == nullptr) {
        return 0.0f;
    }
    const float stated = vertical ? flex->minH : flex->minW;
    const uint8_t mode = vertical ? flex->heightMode : flex->widthMode;
    const float px = vertical ? flex->height : flex->width;
    float base = 64.0f;
    if (stated > 1.0f) {
        base = stated;
    } else if (!ignoreFixed && mode == static_cast<uint8_t>(UiSize::Px) && flex->shrink < 0.5f && px > 1.0f) {
        base = px;
    }
    const bool column = flex->direction == 0 || flex->direction == 2;
    const bool along = vertical == column;
    float acc = along ? 0.0f : base;
    int kids = 0;
    const uint32_t count = uiNodeChildCount(s, id);
    for (uint32_t i = 0; i < count; ++i) {
        const uint16_t child = childAt(s, id, i);
        if (!nodeShown(s, child)) {
            continue;
        }
        const float part = axisFloor(s, child, vertical, false, depth + 1);
        if (along) {
            acc += part;
        } else if (part > acc) {
            acc = part;
        }
        kids += 1;
    }
    if (along && kids > 1 && flex->gap > 0.0f) {
        acc += flex->gap * static_cast<float>(kids - 1);
    }
    if (acc < base) {
        acc = base;
    }
    return acc;
}

void writeSize(UiFlex& flex, bool vertical, float size) {
    if (vertical) {
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = size;
        flex.shrink = 0.0f;
        return;
    }
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = size;
    flex.shrink = 0.0f;
}

} // namespace

uint16_t splitTarget(const UiState& s, uint16_t split) {
    const int index = childIndex(s, split);
    if (index <= 0) {
        return uiFindName(s, "side");
    }
    return childAt(s, s.parentOf[split], static_cast<uint32_t>(index - 1));
}

bool splitVertical(const UiState& s, uint16_t split) {
    const uint16_t parent = s.parentOf[split];
    const UiFlex* flex = parent < s.count ? s.ent[parent].try_get<UiFlex>() : nullptr;
    if (flex == nullptr) {
        return false;
    }
    return flex->direction == 0 || flex->direction == 2;
}

uint8_t splitCursor(const UiState& s, uint16_t split) {
    return splitVertical(s, split) ? 2 : 1;
}

UiBox splitGrip(const UiState& s, uint16_t split) {
    UiBox grip{};
    if (split >= s.count) {
        return grip;
    }
    const UiBox& box = s.box[split];
    if (splitVertical(s, split)) {
        grip.w = box.w;
        grip.h = box.h < 8.0f ? box.h : 8.0f;
        if (grip.h < 6.0f && box.h >= 6.0f) {
            grip.h = 6.0f;
        }
        grip.x = box.x;
        grip.y = box.h > grip.h + 0.5f ? box.y + (box.h - grip.h) * 0.5f : box.y;
        return grip;
    }
    grip.w = box.w < 6.0f ? box.w : 6.0f;
    grip.h = box.h;
    const bool leading = childIndex(s, split) <= 0;
    grip.x = leading || box.w <= 6.0f ? box.x : box.x + box.w - grip.w;
    grip.y = box.y;
    return grip;
}

bool splitAlive(const UiState& s, uint16_t split) {
    const int index = childIndex(s, split);
    if (index < 0) {
        return false;
    }
    const uint16_t parent = s.parentOf[split];
    return childAt(s, parent, static_cast<uint32_t>(index + 1)) != kUiNone || splitTarget(s, split) != kUiNone;
}

void splitDrag(UiState& s, uint16_t split, float x, float y) {
    const int index = childIndex(s, split);
    if (index < 0) {
        return;
    }
    const uint16_t parent = s.parentOf[split];
    const bool vertical = splitVertical(s, split);
    const uint16_t prev = index > 0 ? childAt(s, parent, static_cast<uint32_t>(index - 1)) : kUiNone;
    const uint16_t next = childAt(s, parent, static_cast<uint32_t>(index + 1));
    const UiFlex* prevFlex = prev != kUiNone ? s.ent[prev].try_get<UiFlex>() : nullptr;
    const UiFlex* nextFlex = next != kUiNone ? s.ent[next].try_get<UiFlex>() : nullptr;
    const bool prevSized = prevFlex != nullptr && sized(*prevFlex, vertical);
    const bool nextSized = nextFlex != nullptr && sized(*nextFlex, vertical);
    const bool useNext = nextSized && !prevSized;
    const uint16_t pane = useNext ? next : prev;
    if (pane == kUiNone) {
        return;
    }
    auto* flex = s.ent[pane].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    const UiBox& home = s.box[parent];
    const UiBox& bar = s.box[split];
    float reserved = vertical ? bar.h : bar.w;
    const uint32_t kids = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < kids; ++i) {
        const uint16_t child = childAt(s, parent, i);
        if (child == pane || child == split) {
            continue;
        }
        reserved += axisFloor(s, child, vertical, false, 0);
    }
    const float extent = vertical ? home.h : home.w;
    float limit = extent - reserved;
    const float paneMin = axisFloor(s, pane, vertical, true, 0);
    float size = 0.0f;
    if (vertical) {
        size = useNext ? (home.y + home.h) - y - bar.h : y - s.box[pane].y;
    } else {
        size = useNext ? (home.x + home.w) - x - bar.w : x - s.box[pane].x;
    }
    if (limit < 32.0f) {
        limit = 32.0f;
    }
    if (size > limit) {
        size = limit;
    }
    if (size < paneMin && paneMin <= limit) {
        size = paneMin;
    }
    writeSize(*flex, vertical, size);
    s.layoutDirty = true;
    s.visualDirty = true;
}

namespace {

struct UiSplitHot {
    uint16_t id = kUiNone;
};

void splitPaint(UiState& s, uint16_t id, bool hot) {
    if (id >= s.count || !s.ent[id].is_alive() || s.ent[id].try_get<UiSplitter>() == nullptr) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    const auto token = static_cast<uint8_t>(hot ? UiToken::Accent : UiToken::Muted);
    if (paint->role == static_cast<uint8_t>(UiRole::Panel) && paint->token == token) {
        return;
    }
    const int16_t tag = paint->tag;
    const float radius = paint->radius;
    *paint = paintToken(s.look, UiRole::Panel, static_cast<UiToken>(token), radius, tag);
    s.visualDirty = true;
}

} // namespace

void splitSync(UiState& s) {
    uint16_t hot = kUiNone;
    if (s.capture < s.count && s.ent[s.capture].try_get<UiSplitter>() != nullptr) {
        hot = s.capture;
    } else if (s.hovered < s.count && s.ent[s.hovered].try_get<UiSplitter>() != nullptr) {
        hot = s.hovered;
    }
    UiSplitHot& prev = s.world.ensure<UiSplitHot>();
    if (prev.id == hot) {
        return;
    }
    splitPaint(s, prev.id, false);
    splitPaint(s, hot, true);
    prev.id = hot;
}

} // namespace burnhope

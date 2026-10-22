#include "ui/Panel.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Splitter.hpp"

#include <algorithm>

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
    const UiSize mode = vertical ? flex.heightMode : flex.widthMode;
    return mode == UiSize::Px && flex.grow < 0.5f;
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
    const UiSize mode = vertical ? flex->heightMode : flex->widthMode;
    const float px = vertical ? flex->height : flex->width;
    float base = 64.0f;
    if (stated > 1.0f) {
        base = stated;
    } else if (!ignoreFixed && mode == UiSize::Px && flex->shrink < 0.5f && px > 1.0f) {
        base = px;
    }
    const bool column = flex->direction == UiFlexDir::Column || flex->direction == UiFlexDir::ColumnReverse;
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
        flex.heightMode = UiSize::Px;
        flex.height = size;
        flex.shrink = 0.0f;
        return;
    }
    flex.widthMode = UiSize::Px;
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
    return flex->direction == UiFlexDir::Column || flex->direction == UiFlexDir::ColumnReverse;
}

uint8_t splitCursor(const UiState& s, uint16_t split) {
    if (split < s.count && s.ent[split].has<UiSplitShare>()) {
        return 1;
    }
    return splitVertical(s, split) ? 2 : 1;
}

UiBox splitGrip(const UiState& s, uint16_t split) {
    if (split >= s.count) {
        return {};
    }
    UiBox grip = s.box[split];
    constexpr float kHit = 8.0f;
    const bool alongY = splitVertical(s, split) && !s.ent[split].has<UiSplitShare>();
    if (alongY) {
        if (grip.h < kHit) {
            grip.y -= (kHit - grip.h) * 0.5f;
            grip.h = kHit;
        }
    } else if (grip.w < kHit) {
        grip.x -= (kHit - grip.w) * 0.5f;
        grip.w = kHit;
    }
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

float paneSize(const UiState& s, uint16_t pane, bool vertical) {
    const UiFlex* flex = pane < s.count ? s.ent[pane].try_get<UiFlex>() : nullptr;
    if (flex == nullptr) {
        return vertical ? s.box[pane].h : s.box[pane].w;
    }
    const UiSize mode = vertical ? flex->heightMode : flex->widthMode;
    const float px = vertical ? flex->height : flex->width;
    if (mode == UiSize::Px && px > 1.0f) {
        return px;
    }
    return vertical ? s.box[pane].h : s.box[pane].w;
}

struct PropGrip {
    uint16_t host = kUiNone;
    float label = 140.0f;
    float indent = 28.0f;
};

PropGrip propGrip(const UiState& s, uint16_t split) {
    PropGrip out{};
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropGrid* grid = s.ent[id].try_get<UiPropGrid>();
        if (grid == nullptr || grid->grip != split) {
            continue;
        }
        out.host = id;
        out.label = grid->labelW > 8.0f ? grid->labelW : out.label;
        break;
    }
    if (out.host == kUiNone) {
        return out;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiPropLabel>()) {
            continue;
        }
        bool under = false;
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (p == out.host) {
                under = true;
                break;
            }
        }
        if (!under) {
            continue;
        }
        const uint16_t row = s.parentOf[id];
        if (row < s.count) {
            if (const UiFlex* flex = s.ent[row].try_get<UiFlex>()) {
                if (flex->padL > 1.0f) {
                    out.indent = flex->padL;
                }
            }
        }
        break;
    }
    return out;
}

void shareResize(UiState& s, uint16_t split, float x) {
    auto* drag = s.ent[split].try_get_mut<UiSplitter>();
    if (drag == nullptr) {
        return;
    }
    const PropGrip prop = propGrip(s, split);
    const uint16_t homeId = s.parentOf[split];
    const float span = homeId < s.count ? s.box[homeId].w : 0.0f;
    float size = drag->armed != 0 ? drag->startSize + (x - drag->startMouse) : prop.label;
    const float maxSize = std::max(72.0f, span - prop.indent - 72.0f);
    size = std::clamp(size, 72.0f, maxSize);
    const uint16_t host = prop.host;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiPropLabel>()) {
            continue;
        }
        bool under = false;
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (p == host) {
                under = true;
                break;
            }
        }
        if (!under) {
            continue;
        }
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (flex != nullptr) {
            writeSize(*flex, false, size);
        }
    }
    if (auto* bar = s.ent[split].try_get_mut<UiFlex>()) {
        bar->position = UiPosition::Absolute;
        bar->posX = size + prop.indent;
        bar->widthMode = UiSize::Px;
        bar->width = 1.0f;
    }
    if (host < s.count) {
        if (auto* grid = s.ent[host].try_get_mut<UiPropGrid>()) {
            grid->labelW = size;
        }
    }
    s.layoutFrom = 0;
    s.layoutDirty = true;
    s.visualDirty = true;
}

void splitBegin(UiState& s, uint16_t split, float x, float y) {
    if (split < s.count && s.ent[split].has<UiSplitShare>()) {
        auto* drag = s.ent[split].try_get_mut<UiSplitter>();
        if (drag == nullptr) {
            return;
        }
        const PropGrip prop = propGrip(s, split);
        drag->pane = prop.host != kUiNone ? prop.host : s.parentOf[split];
        drag->useNext = 0;
        drag->startSize = prop.label;
        drag->startMouse = x;
        drag->axis = 0;
        drag->armed = 1;
        return;
    }
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
    auto* drag = s.ent[split].try_get_mut<UiSplitter>();
    if (drag == nullptr) {
        return;
    }
    drag->pane = pane;
    drag->useNext = useNext ? 1 : 0;
    drag->startSize = paneSize(s, pane, vertical);
    drag->startMouse = vertical ? y : x;
    drag->axis = vertical ? 1 : 0;
    drag->armed = 1;
}

void splitDrag(UiState& s, uint16_t split, float x, float y) {
    if (split < s.count && s.ent[split].has<UiSplitShare>()) {
        shareResize(s, split, x);
        return;
    }
    const int index = childIndex(s, split);
    if (index < 0) {
        return;
    }
    auto* drag = s.ent[split].try_get_mut<UiSplitter>();
    if (drag == nullptr) {
        return;
    }
    const uint16_t parent = s.parentOf[split];
    const bool vertical = drag->armed != 0 ? drag->axis != 0 : splitVertical(s, split);
    const uint16_t prev = index > 0 ? childAt(s, parent, static_cast<uint32_t>(index - 1)) : kUiNone;
    const uint16_t next = childAt(s, parent, static_cast<uint32_t>(index + 1));
    const UiFlex* prevFlex = prev != kUiNone ? s.ent[prev].try_get<UiFlex>() : nullptr;
    const UiFlex* nextFlex = next != kUiNone ? s.ent[next].try_get<UiFlex>() : nullptr;
    const bool prevSized = prevFlex != nullptr && sized(*prevFlex, vertical);
    const bool nextSized = nextFlex != nullptr && sized(*nextFlex, vertical);
    const bool useNext = drag->armed != 0 ? drag->useNext != 0 : (nextSized && !prevSized);
    const uint16_t pane = drag->armed != 0 && drag->pane != kUiNone ? drag->pane : (useNext ? next : prev);
    if (pane == kUiNone || pane >= s.count) {
        return;
    }
    auto* flex = s.ent[pane].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    const UiBox& home = s.box[parent];
    const UiBox& bar = s.box[split];
    const float mouse = vertical ? y : x;
    const float origin = vertical ? home.y : home.x;
    const float span = vertical ? home.h : home.w;
    const float grip = vertical ? bar.h : bar.w;
    float size = 0.0f;
    if (drag->armed == 0) {
        size = useNext ? (origin + span - mouse - grip) : (mouse - origin);
    } else {
        float delta = mouse - drag->startMouse;
        if (drag->useNext != 0) {
            delta = -delta;
        }
        size = drag->startSize + delta;
    }
    constexpr float minSize = 48.0f;
    const float maxSize = std::max(minSize, span - grip - minSize);
    size = std::clamp(size, minSize, maxSize);
    writeSize(*flex, vertical, size);
    s.layoutFrom = 0;
    s.layoutDirty = true;
    s.visualDirty = true;
}

namespace {

struct UiSplitHot {
    uint16_t id = kUiNone;
};

void splitPaint(UiState& s, uint16_t id, bool hot) {
    if (id >= s.count || !s.ent[id].is_alive() || !s.ent[id].has<UiSplitter>()) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    if (s.ent[id].has<UiSplitShare>()) {
        if (hot) {
            *paint = paintToken(s.look, UiToken::Accent, 0.0f);
            paint->a = 1.0f;
        } else {
            paint->token = UiToken::Custom;
            paint->r = 1.0f;
            paint->g = 1.0f;
            paint->b = 1.0f;
            paint->a = 0.08f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
        }
        s.visualDirty = true;
        return;
    }
    const UiToken token = hot ? UiToken::Accent : UiToken::Muted;
    if (paint->token == token) {
        return;
    }
    const float radius = paint->radius;
    *paint = paintToken(s.look, token, radius);
    paint->a = hot ? 0.9f : 0.62f;
    s.visualDirty = true;
}

} // namespace

void splitSync(UiState& s, uint16_t hot) {
    UiSplitHot& prev = s.world.ensure<UiSplitHot>();
    if (prev.id == hot) {
        return;
    }
    splitPaint(s, prev.id, false);
    splitPaint(s, hot, true);
    prev.id = hot;
}

namespace {

uint16_t splitAt(const UiState& s, float x, float y) {
    for (uint16_t n = s.count; n > 0; --n) {
        const uint16_t id = static_cast<uint16_t>(n - 1);
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiSplitter>() || !nodeShown(s, id) || !splitAlive(s, id)) {
            continue;
        }
        if (hitBox(splitGrip(s, id), x, y)) {
            return id;
        }
    }
    return kUiNone;
}

} // namespace

bool uiSplitInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor) {
    const uint16_t under = splitAt(s, in.pointer.x, in.pointer.y);
    if (phase == kUiInCursor) {
        if (under == kUiNone) {
            return false;
        }
        cursor = splitCursor(s, under);
        return true;
    }
    if (phase == kUiInPress) {
        if (under == kUiNone) {
            return false;
        }
        if (id < s.count && id != under && s.ent[id].has<UiFocusable>() && !s.ent[id].has<UiSplitter>()) {
            return false;
        }
        s.capture = under;
        splitBegin(s, under, in.pointer.x, in.pointer.y);
        return true;
    }
    if (phase == kUiInDrag) {
        if (id < s.count && s.ent[id].has<UiSplitter>()) {
            splitDrag(s, id, in.pointer.x, in.pointer.y);
            return true;
        }
        return false;
    }
    if (phase == kUiInClip) {
        return s.capture < s.count && s.ent[s.capture].has<UiSplitter>() && s.laidW > 1.0f && s.laidH > 1.0f;
    }
    if (phase == kUiInSync) {
        uint16_t hot = kUiNone;
        if (s.capture < s.count && s.ent[s.capture].has<UiSplitter>()) {
            hot = s.capture;
        } else {
            hot = splitAt(s, in.pointer.x, in.pointer.y);
        }
        splitSync(s, hot);
    }
    return false;
}

uint16_t uiSplitBar(UiState& s, uint16_t parent, bool verticalBar) {
    UiFlex grip{};
    grip.shrink = 0.0f;
    grip.self = UiAlignSelf::Stretch;
    if (verticalBar) {
        grip.widthMode = UiSize::Px;
        grip.width = 2.0f;
        grip.self = UiAlignSelf::Stretch;
    } else {
        grip.heightMode = UiSize::Px;
        grip.height = 2.0f;
        grip.self = UiAlignSelf::Stretch;
    }
    UiPaint paint = paintToken(s.look, UiToken::Muted, 0.0f);
    paint.a = 0.55f;
    const uint16_t bar = uiPanel(s, parent, grip, paint);
    if (bar != kUiNone) {
        s.ent[bar].set<UiSplitter>({});
        s.ent[bar].add<UiFocusable>();
    }
    return bar;
}

void uiSplitterSetWidth(UiState& s, uint16_t split, float width) {
    if (split >= s.count || !s.ent[split].is_alive() || !s.ent[split].has<UiSplitter>()) {
        return;
    }
    const uint16_t pane = splitTarget(s, split);
    if (pane == kUiNone || pane >= s.count) {
        return;
    }
    auto* flex = s.ent[pane].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    writeSize(*flex, splitVertical(s, split), width);
    s.layoutFrom = 0;
    s.layoutDirty = true;
    s.visualDirty = true;
}

} // namespace burnhope

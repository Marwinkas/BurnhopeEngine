#include "ui/Panel.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

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
    float base = uiStyle(s, "splitter_grip", UiStyleKey::BasePane);
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

void writeSize(UiState& s, uint16_t pane, bool vertical, float size) {
    if (vertical) {
        uiSetHeight(s, pane, size);
    } else {
        uiSetWidth(s, pane, size);
    }
    uiFlexShrink(s, pane, 0.0f);
}

// A control, label or tab caption drawn over the grip owns the pointer: the splitter never claims it.
bool coversGrip(const UiState& s, uint16_t id, uint16_t under) {
    if (id >= s.count || id == under || s.ent[id].has<UiSplitter>()) {
        return false;
    }
    if (s.ent[id].has<UiFocusable>()) {
        return true;
    }
    const UiText* text = s.ent[id].try_get<UiText>();
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    return (text != nullptr && text->len > 0) || (paint != nullptr && paint->icon != 0);
}

} // namespace

uint16_t splitTarget(const UiState& s, uint16_t split) {
    const UiSplitter* drag = split < s.count ? s.ent[split].try_get<UiSplitter>() : nullptr;
    if (drag != nullptr && drag->target != kUiNone) {
        return drag->target;
    }
    const int index = childIndex(s, split);
    if (index <= 0) {
        return kUiNone;
    }
    return childAt(s, s.parentOf[split], static_cast<uint32_t>(index - 1));
}

void uiSplitterBind(UiState& s, uint16_t split, uint16_t target, uint8_t axis, UiSplitResizeFn fn, void* user) {
    auto* drag = split < s.count ? s.ent[split].try_get_mut<UiSplitter>() : nullptr;
    if (drag == nullptr) {
        return;
    }
    drag->target = target;
    drag->axis = axis;
    drag->axisLock = 1;
    drag->onResize = fn;
    drag->resizeUser = user;
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
    const UiSplitter* drag = split < s.count ? s.ent[split].try_get<UiSplitter>() : nullptr;
    if (drag != nullptr && drag->axisLock != 0) {
        return drag->axis != 0 ? 2 : 1;
    }
    return splitVertical(s, split) ? 2 : 1;
}

UiBox splitGrip(const UiState& s, uint16_t split) {
    if (split >= s.count) {
        return {};
    }
    UiBox grip = s.box[split];
    const float kHit = uiStyle(s, "splitter_grip", UiStyleKey::HitSlop);
    const UiSplitter* drag = s.ent[split].try_get<UiSplitter>();
    const bool alongY = drag != nullptr && drag->axisLock != 0 ? drag->axis != 0 : splitVertical(s, split);
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
    const UiSplitter* drag = split < s.count ? s.ent[split].try_get<UiSplitter>() : nullptr;
    if (drag != nullptr && drag->onResize != nullptr) {
        return true;
    }
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

void splitBegin(UiState& s, uint16_t split, float x, float y) {
    auto* bound = split < s.count ? s.ent[split].try_get_mut<UiSplitter>() : nullptr;
    if (bound != nullptr && bound->onResize != nullptr) {
        bound->startSize = bound->live > 1.0f ? bound->live : bound->startSize;
        bound->startMouse = bound->axis != 0 ? y : x;
        bound->armed = 1;
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
    auto* bound = split < s.count ? s.ent[split].try_get_mut<UiSplitter>() : nullptr;
    if (bound != nullptr && bound->onResize != nullptr) {
        const float mouse = bound->axis != 0 ? y : x;
        const float size = bound->startSize + (mouse - bound->startMouse);
        bound->live = bound->onResize(bound->resizeUser, split, size);
        s.layoutFrom = 0;
        uiNotifyLayoutDirty(s);
        uiNotifyVisualDirty(s);
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
    const float minSize = uiStyle(s, "splitter_grip", UiStyleKey::MinPane);
    const float maxSize = std::max(minSize, span - grip - minSize);
    size = std::clamp(size, minSize, maxSize);
    writeSize(s, pane, vertical, size);
    s.layoutFrom = 0;
    uiNotifyLayoutDirty(s);
    uiNotifyVisualDirty(s);
}

namespace {

struct UiSplitHot {};

void splitPaint(UiState& s, uint16_t id, bool hot) {
    if (id >= s.count || !s.ent[id].is_alive() || !s.ent[id].has<UiSplitter>()) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    const UiSplitter* drag = s.ent[id].try_get<UiSplitter>();
    if (drag != nullptr && drag->onResize != nullptr) {
        uint16_t peers[kUiCap];
        const uint16_t count = s.ent[id].try_get<UiSplitGroup>() != nullptr ? uiCollect<UiSplitGroup>(s, peers, kUiCap) : uint16_t{0};
        if (count == 0) {
            if (hot) {
                *paint = paintToken(s.look, UiToken::Accent, 0.0f);
                paint->a = 1.0f;
                uiNotifyVisualDirty(s);
            }
            return;
        }
        for (uint16_t i = 0; i < count; ++i) {
            auto* peer = s.ent[peers[i]].try_get_mut<UiPaint>();
            const UiSplitGroup* group = s.ent[peers[i]].try_get<UiSplitGroup>();
            if (peer == nullptr || group == nullptr) {
                continue;
            }
            if (hot) {
                *peer = paintToken(s.look, UiToken::Accent, 0.0f);
                peer->a = 1.0f;
            } else {
                *peer = paintToken(s.look, UiToken::Custom, 0.0f);
                paintTint(*peer, s.look.text);
                peer->a = group->idleA;
            }
        }
        uiNotifyVisualDirty(s);
        return;
    }
    const UiToken token = hot ? UiToken::Accent : UiToken::Muted;
    if (paint->token == token) {
        return;
    }
    const float radius = paint->radius;
    *paint = paintToken(s.look, token, radius);
    paint->a = uiStyle(s, "splitter_grip", hot ? UiStyleKey::HotAlpha : UiStyleKey::IdleAlpha);
    s.visualDirty = true;
}

} // namespace

void splitSync(UiState& s, uint16_t hot) {
    const uint16_t prev = uiMarked<UiSplitHot>(s);
    if (prev == hot) {
        return;
    }
    splitPaint(s, prev, false);
    splitPaint(s, hot, true);
    uiMarkOnly<UiSplitHot>(s, hot);
}

namespace {

uint16_t splitAt(const UiState& s, float x, float y) {
    uint16_t splitters[kUiCap];
    const uint16_t count = uiCollect<UiSplitter>(s, splitters, kUiCap);
    for (uint16_t n = count; n > 0; --n) {
        const uint16_t id = splitters[n - 1];
        if (!nodeShown(s, id) || !splitAlive(s, id)) {
            continue;
        }
        if (hitBox(splitGrip(s, id), x, y)) {
            return id;
        }
    }
    return kUiNone;
}

} // namespace

bool uiSplitInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note) {
    const uint16_t under = splitAt(s, in.pointer.x, in.pointer.y);
    if (phase == kUiInCursor) {
        if (under == kUiNone || coversGrip(s, id, under)) {
            return false;
        }
        note.cursor = splitCursor(s, under);
        return true;
    }
    if (phase == kUiInPress) {
        if (under == kUiNone) {
            return false;
        }
        if (coversGrip(s, id, under)) {
            return false;
        }
        uiCapture(s, under);
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

uint16_t uiSplitBar(UiState& s, uint16_t parent, bool verticalBar, float inset) {
    const float thickness = uiStyle(s, "splitter_grip", UiStyleKey::Width);
    UiFlex grip{};
    grip.shrink = 0.0f;
    grip.self = UiAlignSelf::Stretch;
    if (verticalBar) {
        grip.widthMode = UiSize::Px;
        grip.width = thickness;
        grip.marginT = inset;
    } else {
        grip.heightMode = UiSize::Px;
        grip.height = thickness;
        grip.marginL = inset;
    }
    UiPaint paint = paintToken(s.look, uiStyleBg(s, "splitter_grip"), 0.0f);
    paint.a = uiStyle(s, "splitter_grip", UiStyleKey::Alpha);
    const uint16_t bar = uiPanel(s, parent, grip, paint);
    if (bar != kUiNone) {
        s.ent[bar].set<UiSplitter>({});
        uiSetFocusable(s, bar);
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
    writeSize(s, pane, splitVertical(s, split), width);
    s.layoutFrom = 0;
    uiNotifyLayoutDirty(s);
    uiNotifyVisualDirty(s);
}

uint16_t loadSplit(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char*, const char*, const char*, void*) {
    const bool vertical = node.direction != static_cast<uint8_t>(UiFlexDir::Row);
    return uiSplitBar(s, parent, vertical, vertical ? node.marginT : node.marginL);
}

void uiSplitBin(UiState& s) {
    uiBinRole(s, kBinSlotSplit, "split", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotSplit, loadSplit, nullptr);
}

} // namespace burnhope

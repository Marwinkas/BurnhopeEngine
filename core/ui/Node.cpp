#include "platform/Check.hpp"
#include "Str.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

#include <yoga/Yoga.h>

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

uint16_t uiYogaId(const UiState& s, const YGNode* node) {
    if (node == nullptr) {
        return kUiNone;
    }
    const auto* ctx = static_cast<const UiYogaCtx*>(YGNodeGetContext(node));
    return ctx != nullptr && ctx->id < s.count ? ctx->id : kUiNone;
}

bool nodeShown(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.yoga[p] != nullptr && YGNodeStyleGetDisplay(s.yoga[p]) == YGDisplayNone) {
            return false;
        }
    }
    return true;
}

void uiName(UiState& s, uint16_t id, const char* name) {
    if (id == kUiNone || name == nullptr || name[0] == '\0') {
        return;
    }
    UiName stored{};
    uint8_t n = 0;
    bool plain = true;
    while (name[n] != '\0' && n < 31) {
        const char ch = name[n];
        stored.bytes[n] = ch;
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if (!ok) {
            plain = false;
        }
        ++n;
    }
    stored.bytes[n] = '\0';
    stored.len = n;
    s.ent[id].set<UiName>(stored);
    if (plain) {
        s.ent[id].set_name(stored.bytes);
    }
}

uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    if (s.count >= kUiCap) {
        return kUiNone;
    }
    const uint16_t id = s.count++;
    YGNodeRef node = YGNodeNew();
    s.yogaCtx[id] = {.state = &s, .id = id};
    YGNodeSetContext(node, &s.yogaCtx[id]);
    s.yoga[id] = node;
    s.parentOf[id] = parent;
    if (parent != kUiNone) {
        YGNodeInsertChild(s.yoga[parent], node, YGNodeGetChildCount(s.yoga[parent]));
    }
    flecs::entity e = s.world.entity();
    if (parent != kUiNone) {
        e.child_of(s.ent[parent]);
    }
    e.set<UiSlot>({id});
    e.set<UiFlex>(flex);
    e.set<UiPaint>(paint);
    s.ent[id] = e;
    return id;
}

void addText(UiState& s, uint16_t id, const char* text) {
    uiText(s, id, text);
}

void stamp(UiState& s, uint16_t id, UiToken token) {
    if (id == kUiNone) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint != nullptr) {
        paint->token = token;
    }
}


void uiMarkLayout(UiState& s, uint16_t id) {
    s.layoutDirty = true;
    if (s.layoutFrom == kUiNone) {
        s.layoutFrom = id;
        return;
    }
    if (s.layoutFrom != id) {
        s.layoutFrom = 0;
    }
}

void uiShowOnly(UiState& s, uint16_t id) {
    const uint16_t parent = id < s.count ? s.parentOf[id] : kUiNone;
    if (parent == kUiNone) {
        return;
    }
    const uint32_t n = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t child = uiNodeChild(s, parent, i);
        if (child != kUiNone) {
            uiShow(s, child, child == id);
        }
    }
}

void uiTab(UiState& s, uint16_t id, uint16_t index) {
    if (id < s.count) {
        s.ent[id].set<UiTab>({index});
        s.ent[id].add<UiFocusable>();
    }
}

void uiCursorPush(UiState& s, uint8_t kind) {
    if (s.cursorN < 8) {
        s.cursorStack[s.cursorN++] = kind;
    }
}

void uiCursorPop(UiState& s) {
    if (s.cursorN > 0) {
        --s.cursorN;
    }
}

namespace {

template <typename Tag>
void retag(UiState& s, uint16_t& tagged, uint16_t now) {
    if (tagged == now) {
        return;
    }
    if (tagged < s.count && s.ent[tagged].is_alive()) {
        s.ent[tagged].remove<Tag>();
    }
    if (now < s.count && s.ent[now].is_alive()) {
        s.ent[now].add<Tag>();
    }
    tagged = now;
}

} // namespace

// Interaction tags follow the three live ids: only the nodes that gain or lose the state are touched.
void uiSyncInput(UiState& s) {
    retag<UiHovered>(s, s.hoveredTagged, s.hovered);
    retag<UiFocused>(s, s.focusedTagged, s.focused);
    if (s.captureTagged != s.capture) {
        if (s.captureTagged < s.count && s.ent[s.captureTagged].is_alive()) {
            s.ent[s.captureTagged].remove<UiPressed>();
        }
        retag<UiCapture>(s, s.captureTagged, s.capture);
    }
    if (s.capture < s.count && s.ent[s.capture].is_alive()) {
        s.ent[s.capture].set<UiPressed>({1, s.grabX, s.grabY});
    }
}

void uiCapture(UiState& s, uint16_t id) {
    s.capture = id;
}

void uiReleaseCapture(UiState& s) {
    s.capture = kUiNone;
}

void uiRestyle(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive()) {
            continue;
        }
        auto* paint = s.ent[id].try_get_mut<UiPaint>();
        if (paint == nullptr || paint->token == UiToken::Custom || paint->a <= 0.0f) {
            continue;
        }
        tokenRgb(s.look, paint->token, paint->r, paint->g, paint->b);
        const float radius = tokenRadius(s.look, paint->token);
        if (radius >= 0.0f) {
            paint->radius = radius;
        }
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiClear(UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (s.yoga[i] != nullptr && s.parentOf[i] == kUiNone) {
            YGNodeFreeRecursive(s.yoga[i]);
        }
        s.yoga[i] = nullptr;
    }
    if (s.count > 0 && s.ent[0].is_alive()) {
        s.ent[0].destruct();
    }
    for (uint16_t i = 0; i < kUiCap; ++i) {
        s.yoga[i] = nullptr;
        s.ent[i] = {};
        s.parentOf[i] = kUiNone;
        s.contextOf[i] = kUiNone;
        s.box[i] = {};
        s.anim[i] = {};
        s.vis[i] = {};
        s.yogaCtx[i] = {};
    }
    s.animN = 0;
    s.culledN = 0;
    s.hoveredTagged = kUiNone;
    s.focusedTagged = kUiNone;
    s.captureTagged = kUiNone;
    s.count = 0;
    s.hovered = kUiNone;
    s.focused = kUiNone;
    uiReleaseCapture(s);
    for (UiClearFn hook : s.clearHook) {
        if (hook != nullptr) {
            hook(s);
        }
    }
    s.cursor = 0;
    for (uint16_t i = 0; i < kUiCap; ++i) {
        s.culled[i] = 0;
    }
    s.valueHook.clear();
    s.laidW = -1.0f;
    s.laidH = -1.0f;
}

uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
    if (s.count >= kUiCap) {
        return kUiNone;
    }
    return spawn(s, parent, flex, paint);
}

void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user) {
    if (id >= s.count || fn == nullptr) {
        return;
    }
    s.ent[id].set<UiAction>({fn, user});
}

void uiBindName(UiState& s, const char* name, UiClickFn fn, void* user) {
    uiBind(s, uiFindName(s, name), fn, user);
}

uint16_t uiFindName(const UiState& s, const char* name) {
    if (name == nullptr || name[0] == '\0') {
        return kUiNone;
    }
    const ecs_entity_t symbol = ecs_lookup_symbol(s.world, name, false, false);
    const flecs::entity named = symbol != 0 ? flecs::entity(s.world, symbol) : s.world.lookup(name);
    if (named.id() != 0 && named.is_alive()) {
        const UiSlot* slot = named.try_get<UiSlot>();
        if (slot != nullptr) {
            return slot->index;
        }
    }
    const uint16_t n = s.count < kUiCap ? s.count : kUiCap;
    for (uint16_t id = 0; id < n; ++id) {
        const UiName* stored = s.ent[id].try_get<UiName>();
        if (stored != nullptr && std::strcmp(stored->bytes, name) == 0) {
            return id;
        }
    }
    return kUiNone;
}

void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user) {
    if (id >= s.count || fn == nullptr) {
        return;
    }
    s.ent[id].set<UiDrag>({fn, user});
}

void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user) {
    if (id >= s.count || fn == nullptr) {
        return;
    }
    s.ent[id].set<UiEdit>({fn, user});
}

void uiBlur(UiState& s, uint16_t id, float blur) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->blur = blur < 0.0f ? 0.0f : (blur > 1.0f ? 1.0f : blur);
    s.visualDirty = true;
}

void uiTransform(UiState& s, uint16_t id, float angle, float scaleX, float scaleY) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->angle = angle;
    paint->scaleX = scaleX;
    paint->scaleY = scaleY;
    s.visualDirty = true;
}

void uiFilter(UiState& s, uint16_t id, float bright, float contrast) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->bright = bright;
    paint->contrast = contrast;
    s.visualDirty = true;
}

void uiSetPaint(UiState& s, uint16_t id, float r, float g, float b, float a) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->r = r;
    paint->g = g;
    paint->b = b;
    paint->a = a;
    paint->token = UiToken::Custom;
    s.visualDirty = true;
}

uint16_t uiTextId(const UiState& s, uint16_t id) {
    if (id >= s.count || s.ent[id].try_get<UiText>() == nullptr) {
        return kUiNone;
    }
    return id;
}

void uiTextReadTail(UiState& s, uint16_t id, const BinTail& tail) {
    const uint8_t wrap = tail.raw[5];
    const uint8_t align = tail.raw[6];
    const uint8_t ellipsis = tail.raw[7];
    const uint8_t valignRaw = tail.raw[8];
    const uint8_t deco = tail.raw[9];
    float leading = 0.0f;
    std::memcpy(&leading, tail.raw + 22, sizeof(leading));
    if (wrap == 0 && deco == 0 && leading == 0.0f && align == 0 && ellipsis == 0) {
        return;
    }
    const auto valign = static_cast<UiTextValign>(valignRaw != 0 ? valignRaw : (wrap != 0 ? 1 : 0));
    uiTextStyle(s, id, static_cast<UiTextAlign>(align), static_cast<UiTextEllipsis>(ellipsis), valign, wrap != 0, static_cast<UiTextDeco>(deco), leading);
}

void uiSetDisabled(UiState& s, uint16_t id, bool disabled) {
    if (id >= s.count || !s.ent[id].is_alive()) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr || paint->disabled == disabled) {
        return;
    }
    paint->disabled = disabled;
    uiNotifyVisualDirty(s);
}

void uiTextStyle(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis, UiTextValign valign, bool wrap, UiTextDeco deco, float leading) {
    const uint16_t part = uiTextId(s, id);
    if (part == kUiNone) {
        return;
    }
    auto* text = s.ent[part].try_get_mut<UiText>();
    if (text == nullptr) {
        return;
    }
    text->align = align;
    text->ellipsis = ellipsis;
    text->valign = valign;
    text->wrap = wrap;
    text->deco = deco;
    text->leading = leading;
    s.visualDirty = true;
}

void uiStamp(UiState& s, uint16_t id, UiToken token) {
    stamp(s, id, token);
}

void uiDrop(UiState& s, uint16_t id) {
    if (id >= s.count || s.yoga[id] == nullptr) {
        return;
    }
    uiTrace(s, "drop", id);
    uint16_t stack[kUiCap];
    int n = 0;
    stack[n++] = id;
    for (int i = 0; i < n; ++i) {
        const uint32_t kids = uiNodeChildCount(s, stack[i]);
        for (uint32_t k = 0; k < kids && n < kUiCap; ++k) {
            const uint16_t child = uiNodeChild(s, stack[i], k);
            if (child != kUiNone) {
                stack[n++] = child;
            }
        }
    }
    if (YGNodeRef parent = YGNodeGetParent(s.yoga[id])) {
        YGNodeRemoveChild(parent, s.yoga[id]);
    }
    YGNodeFreeRecursive(s.yoga[id]);
    for (int i = n - 1; i >= 0; --i) {
        const uint16_t node = stack[i];
        s.yoga[node] = nullptr;
        if (s.ent[node].is_alive()) {
            s.ent[node].destruct();
        }
        s.ent[node] = {};
        s.parentOf[node] = kUiNone;
        s.box[node] = {};
        if (s.hovered == node) {
            s.hovered = kUiNone;
        }
        if (s.focused == node) {
            s.focused = kUiNone;
        }
        if (s.capture == node) {
            uiReleaseCapture(s);
        }
    }
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiReparent(UiState& s, uint16_t id, uint16_t parent) {
    if (id >= s.count || parent >= s.count || id == parent) {
        return;
    }
    YGNodeRef node = s.yoga[id];
    YGNodeRef next = s.yoga[parent];
    if (node == nullptr || next == nullptr) {
        return;
    }
    YGNodeRef from = YGNodeGetParent(node);
    if (from == next) {
        return;
    }
    if (from != nullptr) {
        YGNodeRemoveChild(from, node);
    }
    YGNodeInsertChild(next, node, YGNodeGetChildCount(next));
    s.parentOf[id] = parent;
    s.layoutDirty = true;
    uiTrace(s, "reparent", id);
}

void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->position = absolute ? UiPosition::Absolute : UiPosition::Static;
    if (absolute) {
        flex->self = UiAlignSelf::Auto;
    }
    flex->posX = x;
    flex->posY = y;
    if (w > 0.0f) {
        flex->widthMode = UiSize::Px;
        flex->width = w;
    } else if (!absolute) {
        flex->widthMode = UiSize::Percent;
        flex->width = 100.0f;
    }
    if (h > 0.0f) {
        flex->heightMode = UiSize::Px;
        flex->height = h;
    }
    flex->shrink = 0.0f;
    s.layoutDirty = true;
    char note[64];
    std::snprintf(note, sizeof(note), "place %.0f,%.0f %.0fx%.0f", x, y, w, h);
    uiTrace(s, note, id);
}

void uiSetHeight(UiState& s, uint16_t id, float h) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->heightMode = UiSize::Px;
    flex->height = h;
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiRowIndent(UiState& s, uint16_t id, float pad) {
    auto* flex = id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr;
    if (flex == nullptr || flex->padL == pad) {
        return;
    }
    flex->padL = pad;
    uiMarkLayout(s, id);
}

void uiSetWidth(UiState& s, uint16_t id, float w) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->widthMode = UiSize::Px;
    flex->width = w;
    flex->grow = 0.0f;
    flex->shrink = 0.0f;
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiFlexLimit(UiState& s, uint16_t id, float width, float height, float shrink) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    if (width > 0.0f) {
        flex->widthMode = UiSize::Px;
        flex->width = width;
        flex->maxWMode = UiSize::Px;
        flex->maxW = width;
        flex->grow = 0.0f;
    }
    if (height > 0.0f) {
        flex->heightMode = UiSize::Px;
        flex->height = height;
        flex->minH = 0.0f;
    }
    flex->shrink = shrink;
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiFlexShrink(UiState& s, uint16_t id, float shrink) {
    auto* flex = id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr;
    if (flex == nullptr || flex->shrink == shrink) {
        return;
    }
    flex->shrink = shrink;
    uiMarkLayout(s, id);
}

void uiPlaceAbsolute(UiState& s, uint16_t id, float x, float y) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->position = UiPosition::Absolute;
    flex->posX = x;
    flex->posY = y;
    s.layoutDirty = true;
}

uint16_t uiFindChildText(const UiState& s, uint16_t parent) {
    const uint32_t n = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t child = uiNodeChild(s, parent, i);
        if (child < s.count && s.ent[child].has<UiText>()) {
            return child;
        }
    }
    return kUiNone;
}

void uiCenterChildren(UiState& s, uint16_t parent) {
    const uint32_t n = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t id = uiNodeChild(s, parent, i);
        auto* flex = id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr;
        if (flex == nullptr) {
            continue;
        }
        flex->self = UiAlignSelf::Center;
        flex->marginT = 0.0f;
        flex->marginB = 0.0f;
    }
    s.layoutDirty = true;
}

void uiSetDrop(UiState& s, uint16_t id, uint8_t drop) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->drop = static_cast<UiFlexDrop>(drop);
        s.layoutDirty = true;
    }
}

void uiGrow(UiState& s, uint16_t id, float extraW, float h) {
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->width += extraW;
    if (h > 0.0f) {
        flex->heightMode = UiSize::Px;
        flex->height = h;
    }
    s.layoutDirty = true;
}

uint32_t uiNodeChildCount(const UiState& s, uint16_t parent) {
    if (parent >= s.count || s.yoga[parent] == nullptr) {
        return 0;
    }
    return YGNodeGetChildCount(s.yoga[parent]);
}

uint16_t uiNodeChild(const UiState& s, uint16_t parent, uint32_t index) {
    if (parent >= s.count || s.yoga[parent] == nullptr || index >= YGNodeGetChildCount(s.yoga[parent])) {
        return kUiNone;
    }
    YGNodeRef node = YGNodeGetChild(s.yoga[parent], index);
    if (node == nullptr) {
        return kUiNone;
    }
    return uiYogaId(s, node);
}

void uiNodeMoveChild(UiState& s, uint16_t parent, uint16_t child, uint32_t newIndex) {
    if (parent >= s.count || child >= s.count) {
        return;
    }
    YGNodeRef host = s.yoga[parent];
    YGNodeRef node = s.yoga[child];
    if (host == nullptr || node == nullptr) {
        return;
    }
    YGNodeRef from = YGNodeGetParent(node);
    if (from == host) {
        const uint32_t count = YGNodeGetChildCount(host);
        uint32_t old = count;
        for (uint32_t i = 0; i < count; ++i) {
            if (YGNodeGetChild(host, i) == node) {
                old = i;
                break;
            }
        }
        if (old == count) {
            return;
        }
        if (newIndex > count) {
            newIndex = count;
        }
        if (old == newIndex) {
            return;
        }
        YGNodeRemoveChild(host, node);
        if (old < newIndex) {
            --newIndex;
        }
    } else if (from != nullptr) {
        YGNodeRemoveChild(from, node);
    }
    const uint32_t live = YGNodeGetChildCount(host);
    if (newIndex > live) {
        newIndex = live;
    }
    YGNodeInsertChild(host, node, newIndex);
    s.parentOf[child] = parent;
    s.layoutDirty = true;
}

void uiVirtualSetShown(UiState& s, uint16_t slot, bool on) {
    const bool live = slot < s.count && s.yoga[slot] != nullptr && YGNodeStyleGetDisplay(s.yoga[slot]) != YGDisplayNone;
    if (live != on) {
        uiShow(s, slot, on);
    }
}

void uiPaintRgb(UiState& s, uint16_t id, float r, float g, float b) {
    if (id >= s.count) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    paint->token = UiToken::Custom;
    paint->r = r;
    paint->g = g;
    paint->b = b;
    paint->a = 1.0f;
    s.visualDirty = 1;
}

void uiShow(UiState& s, uint16_t id, bool visible) {
    if (id >= s.count || s.yoga[id] == nullptr) {
        return;
    }
    const bool shown = YGNodeStyleGetDisplay(s.yoga[id]) != YGDisplayNone;
    if (shown == visible) {
        return;
    }
    YGNodeStyleSetDisplay(s.yoga[id], visible ? YGDisplayFlex : YGDisplayNone);
    uiTrace(s, visible ? "show" : "hide", id);
    s.layoutDirty = true;
    s.visualDirty = true;
}

UiState::~UiState() {
    uiBarScratchShutdown(*this);
    uiClear(*this);
}

void uiBeforeLayoutAdd(UiState& s, void (*fn)(UiState& s)) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.beforeHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiClearAdd(UiState& s, UiClearFn fn) {
    if (fn == nullptr) {
        return;
    }
    [[maybe_unused]] const bool ok = s.clearHook.add(fn);
    BH_ASSERT(ok, "ui hook registry full");
}

void uiRegisterSpawnHook(UiState& s, uint8_t role, UiSpawnFn fn) {
    if (role < kUiBinRoleCap) {
        s.onSpawn[role] = fn;
    }
}

void uiRegisterRoleFinish(UiState& s, uint8_t role, UiRoleFinishFn fn) {
    if (role < kUiBinRoleCap) {
        s.roleFinish[role] = fn;
    }
}

UiState::UiState()
    : paints(world.query<UiSlot, UiPaint>()) {
    for (uint16_t i = 0; i < kUiCap; ++i) {
        parentOf[i] = kUiNone;
        contextOf[i] = kUiNone;
    }
}

} // namespace burnhope

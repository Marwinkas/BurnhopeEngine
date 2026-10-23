#include "Str.hpp"
#include "ui/State.hpp"
#include "platform/Trace.hpp"

#include <spdlog/spdlog.h>

#include <yoga/Yoga.h>

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

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
    YGNodeSetContext(node, reinterpret_cast<void*>(static_cast<uintptr_t>(id)));
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
    if (token == UiToken::Title) {
        s.ent[id].set<UiTitleBar>({});
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
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == parent) {
            uiShow(s, child, child == id);
        }
    }
}

void uiDragBegin(UiState& s, uint16_t source, const char* kind, const char* bytes) {
    s.dropBus = {};
    s.dropBus.source = source;
    s.dropBus.active = 1;
    if (kind != nullptr) {
        for (int i = 0; i < 15 && kind[i] != '\0'; ++i) {
            s.dropBus.kind[i] = kind[i];
        }
    }
    if (bytes != nullptr) {
        for (int i = 0; i < 63 && bytes[i] != '\0'; ++i) {
            s.dropBus.bytes[i] = bytes[i];
        }
    }
}

void uiDragEnd(UiState& s, uint16_t target) {
    if (s.dropBus.active == 0) {
        return;
    }
    s.dropBus.hover = 0;
    uint16_t accept = target;
    if (s.dropBus.kind[0] != '\0') {
        accept = kUiNone;
        for (uint16_t id = target; id != kUiNone; id = s.parentOf[id]) {
            const UiDropTarget* sink = s.ent[id].try_get<UiDropTarget>();
            if (sink != nullptr && std::strcmp(sink->kind, s.dropBus.kind) == 0) {
                accept = id;
                break;
            }
        }
    }
    s.dropBus.target = accept;
    s.dropBus.active = 0;
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

void uiSyncInput(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto& ent = s.ent[id];
        if (!ent.is_alive()) {
            continue;
        }
        if (id == s.hovered) {
            ent.add<UiHovered>();
        } else {
            ent.remove<UiHovered>();
        }
        if (id == s.focused) {
            ent.add<UiFocused>();
        } else {
            ent.remove<UiFocused>();
        }
        if (id == s.capture) {
            ent.add<UiCapture>();
        } else {
            ent.remove<UiCapture>();
        }
        if (id == s.capture) {
            ent.set<UiPressed>({1, s.grabX, s.grabY});
        } else {
            ent.remove<UiPressed>();
        }
        if (id == s.dragId && s.dropBus.active != 0) {
            ent.set<UiDragState>({s.grabX, s.grabY, s.box[id].x, s.box[id].y, 0, 0, 1});
        } else {
            ent.remove<UiDragState>();
        }
        if (s.dropBus.active != 0 && id == s.hovered) {
            const UiDropTarget* sink = ent.try_get<UiDropTarget>();
            s.dropBus.hover = sink != nullptr && std::strcmp(sink->kind, s.dropBus.kind) == 0 ? 1 : 0;
        }
    }
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
        if (paint->token == UiToken::Button
            || paint->token == UiToken::Field
            || paint->token == UiToken::Menu
            || paint->token == UiToken::Check
            || paint->token == UiToken::Item
            || paint->token == UiToken::Close
            || paint->token == UiToken::Chrome) {
            paint->radius = s.look.radiusSm;
        } else if (paint->token == UiToken::Preview || paint->token == UiToken::Scroll) {
            paint->radius = s.look.radius;
        }
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (paint->token == UiToken::Title) {
            s.ent[id].set<UiTitleBar>({});
            if (flex != nullptr) {
                flex->height = s.look.titleBar;
                flex->heightMode = UiSize::Px;
            }
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
    }
    s.count = 0;
    s.hovered = kUiNone;
    s.focused = kUiNone;
    s.capture = kUiNone;
    s.dragId = kUiNone;
    for (uint8_t hook = 0; hook < s.clearN; ++hook) {
        if (s.clearFn[hook] != nullptr) {
            s.clearFn[hook](s);
        }
    }
    s.cursor = 0;
    for (uint16_t i = 0; i < kUiCap; ++i) {
        s.culled[i] = 0;
    }
    s.cmdCount = 0;
    s.valueN = 0;
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
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] == stack[i] && n < kUiCap) {
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
            s.capture = kUiNone;
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

void nodeLabel(const UiState& s, uint16_t id, char* dst, int cap) {
    dst[0] = '-';
    if (cap > 1) {
        dst[1] = '\0';
    }
    if (id >= s.count || !s.ent[id].is_alive() || cap < 2) {
        return;
    }
    if (const UiName* name = s.ent[id].try_get<UiName>(); name != nullptr && name->len > 0) {
        const int n = name->len < cap - 1 ? name->len : cap - 1;
        std::memcpy(dst, name->bytes, static_cast<size_t>(n));
        dst[n] = '\0';
        return;
    }
    if (const UiText* text = s.ent[id].try_get<UiText>(); text != nullptr && text->len > 0 && text->len < 24) {
        const int n = text->len < cap - 1 ? text->len : cap - 1;
        std::memcpy(dst, text->bytes, static_cast<size_t>(n));
        dst[n] = '\0';
    }
}

std::shared_ptr<spdlog::logger> uiLogger() {
    if (auto channel = spdlog::get("UI")) {
        return channel;
    }
    return spdlog::default_logger();
}

void uiTrace(const UiState& s, const char* fn, uint16_t id) {
    auto log = uiLogger();
    if (log == nullptr) {
        return;
    }
    if (id >= s.count || !s.ent[id].is_alive()) {
        log->info("ui fn={} id={}", fn != nullptr ? fn : "-", static_cast<unsigned>(id));
        return;
    }
    char name[32];
    nodeLabel(s, id, name, 32);
    const UiBox& box = s.box[id];
    log->info(
        "ui fn={} id={} name={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
        fn != nullptr ? fn : "-",
        static_cast<unsigned>(id),
        name,
        static_cast<unsigned>(s.parentOf[id]),
        box.x,
        box.y,
        box.w,
        box.h);
}

void uiDumpLayout(const UiState& s) {
    auto log = uiLogger();
    if (log == nullptr) {
        return;
    }
    log->info("ui layout nodes={} laid={:.0f}x{:.0f} dpi={:.2f}", static_cast<unsigned>(s.count), s.laidW, s.laidH, s.dpi);
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || s.yoga[id] == nullptr) {
            continue;
        }
        if (YGNodeStyleGetDisplay(s.yoga[id]) == YGDisplayNone) {
            continue;
        }
        bool buried = false;
        for (uint16_t up = s.parentOf[id]; up != kUiNone && up < s.count; up = s.parentOf[up]) {
            if (s.yoga[up] != nullptr && YGNodeStyleGetDisplay(s.yoga[up]) == YGDisplayNone) {
                buried = true;
                break;
            }
        }
        if (buried) {
            continue;
        }
        const UiName* name = s.ent[id].try_get<UiName>();
        const bool named = name != nullptr && name->len > 0;
        const bool control = s.ent[id].has<UiFocusable>();
        if (!named && !control) {
            continue;
        }
        char label[32];
        nodeLabel(s, id, label, 32);
        const UiBox& box = s.box[id];
        const bool bad = box.w != box.w || box.h != box.h || box.w < 1.0f || box.h < 1.0f || box.x + box.w < 0.0f || box.y + box.h < 0.0f
            || (s.laidW > 0.0f && box.x > s.laidW) || (s.laidH > 0.0f && box.y > s.laidH);
        log->log(
            bad ? spdlog::level::warn : spdlog::level::info,
            "ui {} id={} name={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
            bad ? "miss" : "box",
            static_cast<unsigned>(id),
            label,
            static_cast<unsigned>(s.parentOf[id]),
            box.x,
            box.y,
            box.w,
            box.h);
    }
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
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.yoga[id] == node) {
            return id;
        }
    }
    return kUiNone;
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
    uiClear(*this);
}

namespace {

struct BootTable {
    UiBootFn fn[4]{};
    uint8_t n = 0;
};

BootTable& boots() {
    static BootTable table;
    return table;
}

} // namespace

void uiBootAdd(UiBootFn fn) {
    if (fn == nullptr) {
        return;
    }
    BootTable& table = boots();
    for (uint8_t i = 0; i < table.n; ++i) {
        if (table.fn[i] == fn) {
            return;
        }
    }
    if (table.n < 4) {
        table.fn[table.n++] = fn;
    }
}

void uiClearAdd(UiState& s, UiClearFn fn) {
    if (fn == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < s.clearN; ++i) {
        if (s.clearFn[i] == fn) {
            return;
        }
    }
    if (s.clearN < 4) {
        s.clearFn[s.clearN++] = fn;
    }
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
    const BootTable& table = boots();
    for (uint8_t i = 0; i < table.n; ++i) {
        if (table.fn[i] != nullptr) {
            table.fn[i](*this);
        }
    }
}

} // namespace burnhope

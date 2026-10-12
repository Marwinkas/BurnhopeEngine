#include "ui/widget/Chrome.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/Domain.hpp"
#include "ui/Layout.hpp"
#include "platform/Trace.hpp"

#include <spdlog/spdlog.h>

#include <yoga/Yoga.h>

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

bool nodeShown(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        if (s.yoga[p] != nullptr && YGNodeStyleGetDisplay(s.yoga[p]) == YGDisplayNone) {
            return false;
        }
    }
    return true;
}

uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint) {
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
    const bool ownsText = paint.role == static_cast<uint8_t>(UiRole::Field)
        || paint.role == static_cast<uint8_t>(UiRole::Button)
        || paint.role == static_cast<uint8_t>(UiRole::Close)
        || paint.role == static_cast<uint8_t>(UiRole::Check);
    if (ownsText) {
        UiText host{};
        host.align = paint.role == static_cast<uint8_t>(UiRole::Check) || paint.role == static_cast<uint8_t>(UiRole::Field) ? 0 : 1;
        host.ellipsis = paint.role == static_cast<uint8_t>(UiRole::Field) ? 0 : 1;
        host.valign = 1;
        s.ent[id].set<UiText>(host);
    }
    if (paint.role == static_cast<uint8_t>(UiRole::Check)) {
        if (auto* hostFlex = s.ent[id].try_get_mut<UiFlex>()) {
            if (hostFlex->padL < 32.0f) {
                hostFlex->padL = 32.0f;
            }
        }
    }
    uiNoteChrome(s, id);
    return id;
}

void addText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count) {
        return;
    }
    char bytes[96]{};
    const uint8_t n = uiStrCopy(bytes, text);
    UiText* live = s.ent[id].try_get_mut<UiText>();
    if (live != nullptr && live->len == n && (n == 0 || std::memcmp(live->bytes, bytes, n) == 0)) {
        return;
    }
    if (live == nullptr) {
        UiText created{};
        std::memcpy(created.bytes, bytes, static_cast<size_t>(n) + 1u);
        created.len = n;
        s.ent[id].set<UiText>(created);
        live = s.ent[id].try_get_mut<UiText>();
        if (live == nullptr) {
            return;
        }
    } else {
        std::memcpy(live->bytes, bytes, static_cast<size_t>(n) + 1u);
        live->len = n;
    }
    s.visualDirty = true;
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint != nullptr
        && (paint->role == static_cast<uint8_t>(UiRole::Button) || paint->role == static_cast<uint8_t>(UiRole::Close)
            || paint->role == static_cast<uint8_t>(UiRole::Check))) {
        auto* host = s.ent[id].try_get_mut<UiFlex>();
        if (host != nullptr && host->widthMode == static_cast<uint8_t>(UiSize::Auto) && live->len > 0) {
            host->widthMode = static_cast<uint8_t>(UiSize::Px);
            host->width = uiMeasure(s, live->bytes, live->len, live->len) + 16.0f;
            s.layoutDirty = true;
        }
    }
    if (paint == nullptr || paint->role != static_cast<uint8_t>(UiRole::Label)) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    if (live->wrap != 0) {
        s.layoutDirty = true;
        return;
    }
    const float measured = uiMeasure(s, live->bytes, live->len, live->len);
    const bool wasPx = flex->widthMode == static_cast<uint8_t>(UiSize::Px);
    const float oldW = flex->width;
    const float oldH = flex->height;
    flex->widthMode = static_cast<uint8_t>(UiSize::Px);
    flex->width = measured;
    flex->heightMode = static_cast<uint8_t>(UiSize::Px);
    flex->height = s.look.glyphH;
    if (wasPx && std::fabs(oldW - measured) <= 0.5f && std::fabs(oldH - s.look.glyphH) <= 0.5f) {
        return;
    }
    const uint16_t parent = s.parentOf[id];
    const UiFlex* pflex = parent < s.count ? s.ent[parent].try_get<UiFlex>() : nullptr;
    if (wasPx && pflex != nullptr && pflex->direction == 0 && std::fabs(oldH - s.look.glyphH) <= 0.5f) {
        s.box[id].w = measured;
        return;
    }
    s.layoutDirty = true;
}

void tokenRgb(const UiLook& look, UiToken token, float& r, float& g, float& b) {
    const float* src = look.window;
    switch (token) {
    case UiToken::Title: src = look.title; break;
    case UiToken::Side: src = look.side; break;
    case UiToken::Content: src = look.content; break;
    case UiToken::Button: src = look.button; break;
    case UiToken::Field: src = look.field; break;
    case UiToken::Menu: src = look.menu; break;
    case UiToken::Close: src = look.close; break;
    case UiToken::Text: src = look.text; break;
    case UiToken::Muted: src = look.muted; break;
    case UiToken::Item: src = look.item; break;
    case UiToken::Preview: src = look.preview; break;
    case UiToken::Status: src = look.status; break;
    case UiToken::Slider: src = look.slider; break;
    case UiToken::Check: src = look.check; break;
    case UiToken::Scroll: src = look.content; break;
    case UiToken::Chrome: src = look.chrome; break;
    case UiToken::Accent: src = look.accent[0]; break;
    default: break;
    }
    r = src[0];
    g = src[1];
    b = src[2];
}

void stamp(UiState& s, uint16_t id, UiToken token) {
    if (id == kUiNone) {
        return;
    }
    auto* paint = s.ent[id].try_get_mut<UiPaint>();
    if (paint != nullptr) {
        paint->token = static_cast<uint8_t>(token);
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
        const UiPaint* paint = ent.try_get<UiPaint>();
        if (paint != nullptr && paint->role == static_cast<uint8_t>(UiRole::Hidden)) {
            ent.add<UiPass>();
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
        if (paint == nullptr || paint->token == static_cast<uint8_t>(UiToken::Custom) || paint->a <= 0.0f) {
            continue;
        }
        tokenRgb(s.look, static_cast<UiToken>(paint->token), paint->r, paint->g, paint->b);
        if (paint->token == static_cast<uint8_t>(UiToken::Button)
            || paint->token == static_cast<uint8_t>(UiToken::Field)
            || paint->token == static_cast<uint8_t>(UiToken::Menu)
            || paint->token == static_cast<uint8_t>(UiToken::Check)
            || paint->token == static_cast<uint8_t>(UiToken::Item)
            || paint->token == static_cast<uint8_t>(UiToken::Close)
            || paint->token == static_cast<uint8_t>(UiToken::Chrome)) {
            paint->radius = s.look.radiusSm;
        } else if (paint->token == static_cast<uint8_t>(UiToken::Preview) || paint->token == static_cast<uint8_t>(UiToken::Scroll)) {
            paint->radius = s.look.radius;
        }
        auto* text = s.ent[id].try_get_mut<UiText>();
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (text != nullptr && flex != nullptr && paint->role == static_cast<uint8_t>(UiRole::Label) && text->wrap == 0
            && flex->widthMode != static_cast<uint8_t>(UiSize::Percent)) {
            flex->width = uiMeasure(s, text->bytes, text->len, text->len);
            flex->height = s.look.glyphH;
            flex->widthMode = static_cast<uint8_t>(UiSize::Px);
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        }
        if (paint->token == static_cast<uint8_t>(UiToken::Title)) {
            s.ent[id].set<UiTitleBar>({});
            if (flex != nullptr) {
                flex->height = s.look.titleBar;
                flex->heightMode = static_cast<uint8_t>(UiSize::Px);
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
    uiClearDomain(s);
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

void uiFold(UiState& s, uint16_t id) {
    const uint16_t header = s.parentOf[id];
    const uint16_t panel = header == kUiNone ? kUiNone : s.parentOf[header];
    if (panel == kUiNone) {
        return;
    }
    bool open = false;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != panel || child == header || s.yoga[child] == nullptr) {
            continue;
        }
        if (YGNodeStyleGetDisplay(s.yoga[child]) != YGDisplayNone) {
            open = true;
        }
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == panel && child != header) {
            uiShow(s, child, !open);
        }
    }
    auto* flex = s.ent[panel].try_get_mut<UiFlex>();
    if (flex != nullptr && flex->position != 0) {
        UiFoldState& fold = s.ent[panel].ensure<UiFoldState>();
        if (open) {
            fold.openH = flex->height > 56.0f ? flex->height : s.box[panel].h;
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->height = 52.0f;
        } else if (fold.openH > 56.0f) {
            flex->heightMode = static_cast<uint8_t>(UiSize::Px);
            flex->height = fold.openH;
        }
        s.layoutDirty = true;
    }
}

void onMenuCommand(void* user, uint16_t, int16_t tag) {
    auto& s = *static_cast<UiState*>(user);
    if (tag >= 100) {
        const uint8_t index = static_cast<uint8_t>(tag - 100);
        if (index < s.cmdCount && s.cmds[index].fn != nullptr) {
            s.cmds[index].fn(s.cmds[index].user);
        }
    }
    if (uiMenuMetrics(s).opened != kUiNone) {
        uiShow(s, uiMenuMetrics(s).opened, false);
        uiMenuMetrics(s).opened = kUiNone;
        s.menuOpen = false;
    }
    s.visualDirty = true;
}

void uiText(UiState& s, uint16_t id, const char* text) {
    addText(s, id, text);
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
    paint->token = static_cast<uint8_t>(UiToken::Custom);
    s.visualDirty = true;
}

uint16_t uiTextId(const UiState& s, uint16_t id) {
    if (id >= s.count || s.ent[id].try_get<UiText>() == nullptr) {
        return kUiNone;
    }
    return id;
}

void uiTextStyle(UiState& s, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading) {
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

void uiSetFieldText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count) {
        return;
    }
    auto* field = s.ent[id].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    const uint8_t n = uiStrCopy(field->bytes, text);
    field->len = n;
    field->caret = n;
    field->anchor = n;
    field->scroll = 0.0f;
    fieldMark(s, id);
    fieldSyncText(s, id);
    s.visualDirty = true;
}

void uiStamp(UiState& s, uint16_t id, UiToken token) {
    stamp(s, id, token);
}

bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user) {
    if (s.cmdCount >= kUiCmdCap || s.menu == kUiNone) {
        return false;
    }
    UiCommand& cmd = s.cmds[s.cmdCount];
    copyText(cmd.name, cmd.len, name);
    cmd.fn = fn;
    cmd.user = user;
    const int16_t tag = static_cast<int16_t>(100 + s.cmdCount);
    UiFlex itemF{};
    itemF.heightMode = static_cast<uint8_t>(UiSize::Px);
    itemF.height = uiMenuMetrics(s).itemH;
    itemF.widthMode = static_cast<uint8_t>(UiSize::Percent);
    itemF.width = 100.0f;
    itemF.shrink = 0.0f;
    itemF.align = 1;
    const uint16_t item = spawn(s, s.menu, itemF, paint(UiRole::Button, 0.18f, 0.19f, 0.24f, 4.0f, tag));
    stamp(s, item, UiToken::Button);
    UiText label{};
    copyText(label.bytes, label.len, name);
    s.ent[item].set<UiText>(label);
    uiBind(s, item, onMenuCommand, &s);
    ++s.cmdCount;
    s.layoutDirty = true;
    return item != kUiNone;
}

bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user) {
    const uint16_t prev = s.menu;
    s.menu = menu;
    const bool ok = uiCommandAdd(s, name, fn, user);
    s.menu = prev;
    return ok;
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
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->position = absolute ? 1 : 0;
    flex->posX = x;
    flex->posY = y;
    if (w > 0.0f) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Px);
        flex->width = w;
    } else if (!absolute) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Percent);
        flex->width = 100.0f;
    }
    if (h > 0.0f) {
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
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
    flex->heightMode = static_cast<uint8_t>(UiSize::Px);
    flex->height = h;
    s.layoutDirty = true;
    s.visualDirty = true;
}

void uiSetDrop(UiState& s, uint16_t id, uint8_t drop) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->drop = drop;
        s.layoutDirty = true;
    }
}

bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV) {
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    value = range->value;
    minV = range->min;
    maxV = range->max;
    return true;
}

void uiGrow(UiState& s, uint16_t id, float extraW, float h) {
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->width += extraW;
    if (h > 0.0f) {
        flex->heightMode = static_cast<uint8_t>(UiSize::Px);
        flex->height = h;
    }
    s.layoutDirty = true;
}

const char* roleName(uint8_t role) {
    switch (static_cast<UiRole>(role)) {
    case UiRole::Panel:
        return "panel";
    case UiRole::Button:
        return "button";
    case UiRole::Label:
        return "label";
    case UiRole::Slider:
        return "slider";
    case UiRole::Check:
        return "check";
    case UiRole::Field:
        return "field";
    case UiRole::Close:
        return "close";
    case UiRole::Scroll:
        return "scroll";
    case UiRole::Hidden:
        return "hidden";
    case UiRole::Progress:
        return "progress";
    }
    return "role";
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
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    const UiBox& box = s.box[id];
    const int tag = paint != nullptr ? paint->tag : -1;
    const char* role = paint != nullptr ? roleName(paint->role) : "-";
    log->info(
        "ui fn={} id={} name={} role={} tag={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
        fn != nullptr ? fn : "-",
        static_cast<unsigned>(id),
        name,
        role,
        tag,
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
        const UiPaint* paint = s.ent[id].try_get<UiPaint>();
        if (paint == nullptr) {
            continue;
        }
        const auto role = static_cast<UiRole>(paint->role);
        const UiName* name = s.ent[id].try_get<UiName>();
        const bool named = name != nullptr && name->len > 0;
        const bool control = role == UiRole::Button || role == UiRole::Slider || role == UiRole::Field || role == UiRole::Check
            || role == UiRole::Close || role == UiRole::Scroll;
        if (!named && !control) {
            continue;
        }
        if (role == UiRole::Hidden || role == UiRole::Label) {
            continue;
        }
        char label[32];
        nodeLabel(s, id, label, 32);
        const UiBox& box = s.box[id];
        const bool bad = box.w != box.w || box.h != box.h || box.w < 1.0f || box.h < 1.0f || box.x + box.w < 0.0f || box.y + box.h < 0.0f
            || (s.laidW > 0.0f && box.x > s.laidW) || (s.laidH > 0.0f && box.y > s.laidH);
        log->log(
            bad ? spdlog::level::warn : spdlog::level::info,
            "ui {} id={} name={} role={} parent={} box={:.1f},{:.1f} {:.1f}x{:.1f}",
            bad ? "miss" : "box",
            static_cast<unsigned>(id),
            label,
            roleName(paint->role),
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

void uiShow(UiState& s, uint16_t id, bool visible) {
    if (id >= s.count || s.yoga[id] == nullptr) {
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

UiState::UiState()
    : paints(world.query<UiSlot, UiPaint>()) {
    for (uint16_t i = 0; i < kUiCap; ++i) {
        parentOf[i] = kUiNone;
        contextOf[i] = kUiNone;
    }
    (void)world.ensure<UiHostNote>();
    (void)world.ensure<UiDock>();
    uiInstallChromeBin(*this);
    uiInstallDockOverlay(*this);
}

} // namespace burnhope

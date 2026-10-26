#pragma once

#include "platform/Input.hpp"
#include "ui/Flex.hpp"
#include "ui/Geometry.hpp"
#include "ui/Interaction.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/Metrics.hpp"
#include "ui/Primitive.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/Typography.hpp"
#include "ui/UiBin.hpp"

#include <flecs.h>

typedef struct YGNode* YGNodeRef;

namespace burnhope {

struct UiState;

// Yoga node context: the owning state and the node slot, so measure callbacks need no thread-local.
struct UiYogaCtx {
    UiState* state = nullptr;
    uint16_t id = kUiNone;
};

using UiInputFn = bool (*)(UiState& s, const InputFrame& in);
using UiDrawFn = bool (*)(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiOverlayDrawFn = void (*)(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiTextGateFn = bool (*)(const UiState& s);
using UiDropFn = void (*)(UiState& s);
using UiClearFn = void (*)(UiState& s);
using UiSpawnFn = void (*)(UiState& s, uint16_t id);
struct UiValueListen {
    UiValueFn fn = nullptr;
    void* user = nullptr;
    bool operator==(const UiValueListen&) const = default;
};
inline constexpr uint8_t kUiHookCap = 32;
// Fixed-capacity registry of callbacks: no heap, registration order is call order, duplicates are ignored.
template <class T>
struct UiHookList {
    T fn[kUiHookCap]{};
    uint8_t n = 0;
    [[nodiscard]] bool add(const T& f) {
        for (uint8_t i = 0; i < n; ++i) {
            if (fn[i] == f) {
                return true;
            }
        }
        if (n >= kUiHookCap) {
            return false;
        }
        fn[n++] = f;
        return true;
    }
    void clear() { n = 0; }
    [[nodiscard]] std::size_t size() const { return n; }
    const T& operator[](std::size_t i) const { return fn[i]; }
    const T* begin() const { return fn; }
    const T* end() const { return fn + n; }
};
// Per-node results of one top-down pass (uiCacheVisibility); hit testing and emission read them in O(1).
struct UiNodeVis {
    UiBox clip{};
    uint8_t hidden = 0;
    uint8_t overlay = 0;
    uint8_t clipped = 0;
};
using UiRoleFinishFn = void (*)(UiState& s, uint16_t id, const BinTail* tail, const char* text, const char* hint);
struct UiNoHit {}; // Пустой тег: объект нематериален для курсора мыши
struct UiPaintLast {}; // Среди братьев рисуется последним, поверх непрозрачных строк.
struct UiReveal {
    float full = 0.0f;
    float from = 0.0f;
    float to = 0.0f;
    float t0 = -1.0f;
    uint8_t phase = 0;
};
// A higher module (files, later others) plugs in here. Widgets never name it.
struct UiPlug {
    UiInputFn handle = nullptr;
    UiDrawFn draw = nullptr;
    UiOverlayDrawFn over = nullptr;
    UiTextGateFn wantsText = nullptr;
    UiDropFn shutdown = nullptr;
    bool (*frame)(const UiState& s, uint16_t id) = nullptr;
    void (*move)(UiState& s, uint16_t panel, float x, float y) = nullptr;
    void (*drop)(UiState& s, uint16_t panel) = nullptr;
};

using UiInputHookFn = bool (*)(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

struct UiState {
    flecs::world world;
    flecs::query<UiSlot, UiPaint> paints;
    YGNodeRef yoga[kUiCap]{};
    flecs::entity ent[kUiCap]{};
    UiBox box[kUiCap]{};
    uint16_t parentOf[kUiCap]{};
    uint16_t contextOf[kUiCap]{};
    uint8_t tight[kUiCap]{};
    uint16_t count = 0;
    uint16_t hovered = kUiNone;
    uint8_t cursorStack[8]{};
    uint8_t cursorN = 0;
    uint16_t focused = kUiNone;
    uint16_t capture = kUiNone;
    UiHookList<UiInputHookFn> inputHook;
    UiHookList<UiDrawFn> drawHook;
    UiHookList<UiDrawFn> underHook;
    UiHookList<UiDrawFn> decorHook;
    UiHookList<UiOverlayDrawFn> overlayHook;
    UiHookList<void (*)(UiState&)> placeHook;
    UiHookList<UiValueListen> valueHook;
    UiPlug plug{};
    uint8_t cursor = 0;
    uint8_t culled[kUiCap]{};
    uint16_t culledList[kUiFlexDropVictimCap]{};
    uint16_t culledN = 0;
    UiNodeVis vis[kUiCap]{};
    uint16_t hoveredTagged = kUiNone;
    uint16_t focusedTagged = kUiNone;
    uint16_t captureTagged = kUiNone;
    uint16_t animList[kUiCap]{};
    uint16_t animN = 0;
    UiYogaCtx yogaCtx[kUiCap]{};
    float grabX = 0;
    float grabY = 0;
    UiClickFn onClick = nullptr;
    void* clickUser = nullptr;
    UiGrabFn grabFn = nullptr;
    void* grabUser = nullptr;
    UiFocusFn focusFn = nullptr;
    void* focusUser = nullptr;
    float time = 0;
    float prevTime = 0;
    bool altLook = false;
    UiLook look{};
    UiMetrics metrics{};
    UiFontAtlas font{};
    UiAnim anim[kUiCap]{};
    float laidW = -1.0f;
    float laidH = -1.0f;
    bool layoutDirty = true;
    bool visualDirty = true;
    uint8_t dumpLayout = 0;
    uint16_t layoutFrom = kUiNone;
    UiLayoutFinishHook layoutFinishHook{};
    uint8_t relative = 0;
    uint8_t ateWheel = 0;
    float dpi = 1;
    uint8_t clipOn = 0;
    UiBox clipBox{};
    UiCustomBinLoader binHooks[kUiBinRoleCap]{};
    UiBinRole binRole[kUiBinRoleCap]{};
    UiHookList<UiClearFn> clearHook;
    UiSpawnFn onSpawn[kUiBinRoleCap]{};
    UiRoleFinishFn roleFinish[kUiBinRoleCap]{};
    UiHookList<void (*)(UiState&)> beforeHook;

    UiState();
    ~UiState();
    UiState(const UiState&) = delete;
    UiState& operator=(const UiState&) = delete;
};

void uiFocus(UiState& s, uint16_t id);
void uiFocusStep(UiState& s, int dir);
void uiTab(UiState& s, uint16_t id, uint16_t index);
void uiCursorPush(UiState& s, uint8_t kind);
void uiCursorPop(UiState& s);
void uiSyncInput(UiState& s);
[[nodiscard]] uint16_t uiYogaId(const UiState& s, const struct YGNode* node);
void uiCacheVisibility(UiState& s);
void uiCapture(UiState& s, uint16_t id);
void uiReleaseCapture(UiState& s);
void uiMarkLayout(UiState& s, uint16_t id);
void uiShowOnly(UiState& s, uint16_t id);
void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user);
void uiBindName(UiState& s, const char* name, UiClickFn fn, void* user);
void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user);
void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user);
void uiShow(UiState& s, uint16_t id, bool visible);
void uiPaintRgb(UiState& s, uint16_t id, float r, float g, float b);
void uiReparent(UiState& s, uint16_t id, uint16_t parent);
void uiDrop(UiState& s, uint16_t id);
void uiTrace(const UiState& s, const char* fn, uint16_t id);
void uiDumpLayout(const UiState& s);
void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h);
void uiGrow(UiState& s, uint16_t id, float extraW, float h);
void uiSetHeight(UiState& s, uint16_t id, float h);
void uiSetWidth(UiState& s, uint16_t id, float w);
void uiFlexLimit(UiState& s, uint16_t id, float width, float height, float shrink);
void uiFlexShrink(UiState& s, uint16_t id, float shrink);
void uiRowIndent(UiState& s, uint16_t id, float pad);
void uiPlaceAbsolute(UiState& s, uint16_t id, float x, float y);
uint16_t uiFindChildText(const UiState& s, uint16_t parent);
void uiCenterChildren(UiState& s, uint16_t parent);
void uiSetDrop(UiState& s, uint16_t id, uint8_t drop);
[[nodiscard]] uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text);
[[nodiscard]] uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size);
[[nodiscard]] uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiFindName(const UiState& s, const char* name);
void uiRequestTop(UiState& s, bool on);
void uiRequestOpacity(UiState& s, float opacity);
void uiRequestMinimize(UiState& s);
void uiRequestMaximize(UiState& s);
void uiRequestClose(UiState& s);
inline void uiNotifyLayoutDirty(UiState& s) { s.layoutDirty = true; }
inline void uiNotifyVisualDirty(UiState& s) { s.visualDirty = true; }
inline void uiSetContextMenu(UiState& s, uint16_t node, uint16_t menu) {
    if (node < s.count) {
        s.contextOf[node] = menu;
    }
}
inline uint16_t uiParent(const UiState& s, uint16_t id) {
    return id < s.count ? s.parentOf[id] : kUiNone;
}
inline UiBox uiNodeBox(const UiState& s, uint16_t id) {
    return id < s.count ? s.box[id] : UiBox{};
}
// First node other than `except` holding Tag. Flecs walks only the tables that carry Tag, so the cost follows
// the number of holders, not s.count.
template <typename Tag>
uint16_t uiMarkedOther(const UiState& s, uint16_t except) {
    uint16_t found = kUiNone;
    s.world.each(s.world.id<Tag>(), [&](flecs::entity e) {
        const UiSlot* slot = found == kUiNone ? e.try_get<UiSlot>() : nullptr;
        if (slot != nullptr && slot->index != except && slot->index < s.count && s.ent[slot->index] == e) {
            found = slot->index;
        }
    });
    return found;
}

template <typename Tag>
void uiMarkOnly(UiState& s, uint16_t id) {
    for (uint16_t other = uiMarkedOther<Tag>(s, id); other != kUiNone; other = uiMarkedOther<Tag>(s, id)) {
        s.ent[other].remove<Tag>();
    }
    if (id < s.count && s.ent[id].is_alive()) {
        s.ent[id].add<Tag>();
    }
}

// Slots (ascending) of every live node carrying T, up to `cap`. Flecs walks only the tables holding T.
template <typename T>
uint16_t uiCollect(const UiState& s, uint16_t* out, uint16_t cap) {
    uint16_t n = 0;
    s.world.each(s.world.id<T>(), [&](flecs::entity e) {
        const UiSlot* slot = n < cap ? e.try_get<UiSlot>() : nullptr;
        if (slot != nullptr && slot->index < s.count && s.ent[slot->index] == e) {
            out[n++] = slot->index;
        }
    });
    for (uint16_t i = 1; i < n; ++i) {
        const uint16_t v = out[i];
        uint16_t j = i;
        while (j > 0 && out[j - 1] > v) {
            out[j] = out[j - 1];
            --j;
        }
        out[j] = v;
    }
    return n;
}

template <typename Tag>
uint16_t uiMarked(const UiState& s) {
    return uiMarkedOther<Tag>(s, kUiNone);
}

void uiName(UiState& s, uint16_t id, const char* name);
void uiDrawAdd(UiState& s, UiDrawFn fn);
void uiDrawUnderAdd(UiState& s, UiDrawFn fn);
void uiDrawDecorAdd(UiState& s, UiDrawFn fn);
void uiOverlayAdd(UiState& s, UiOverlayDrawFn fn);
void uiPlaceAdd(UiState& s, void (*fn)(UiState& s));
void uiBeforeLayoutAdd(UiState& s, void (*fn)(UiState& s));
void uiClearAdd(UiState& s, UiClearFn fn);
void uiRegisterSpawnHook(UiState& s, uint8_t role, UiSpawnFn fn);
void uiRegisterRoleFinish(UiState& s, uint8_t role, UiRoleFinishFn fn);
void uiInputAdd(UiState& s, UiInputHookFn fn);
bool nodeShown(const UiState& s, uint16_t id);
uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void addText(UiState& s, uint16_t id, const char* text);
uint16_t hitTest(const UiState& s, float x, float y);
void activate(UiState& s, uint16_t id);
uint32_t uiNodeChildCount(const UiState& s, uint16_t parent);
uint16_t uiNodeChild(const UiState& s, uint16_t parent, uint32_t index);
void uiNodeMoveChild(UiState& s, uint16_t parent, uint16_t child, uint32_t newIndex);

void uiClear(UiState& s);
uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void stamp(UiState& s, uint16_t id, UiToken token);
void uiStamp(UiState& s, uint16_t id, UiToken token);
void uiBlur(UiState& s, uint16_t id, float blur);
void uiTransform(UiState& s, uint16_t id, float angle, float scaleX, float scaleY);
void uiFilter(UiState& s, uint16_t id, float bright, float contrast);
void uiSetPaint(UiState& s, uint16_t id, float r, float g, float b, float a);
[[nodiscard]] UiEvent uiApplyInput(UiState& s, const InputFrame& in);
void uiLayout(UiState& s, float w, float h);
void uiRestyle(UiState& s);
inline void uiApplyLook(UiState& s, bool warm) {
    const float glyphH = s.look.glyphH;
    const float glyphW = s.look.glyphW;
    const float advance = s.look.advance;
    const float title = s.look.titleBar;
    const float border = s.look.resizeBorder;
    s.altLook = warm;
    s.look = warm ? uiLookWarm() : UiLook{};
    s.look.glyphH = glyphH;
    s.look.glyphW = glyphW;
    s.look.advance = advance;
    s.look.titleBar = title;
    s.look.resizeBorder = border;
    uiRestyle(s);
}
void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a);
void uiReveal(UiState& s, uint16_t id, float full, bool open);
[[nodiscard]] float uiRevealUnit(const UiState& s, uint16_t id);
void uiRevealTick(UiState& s);
[[nodiscard]] bool uiTickMotion(UiState& s);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);

bool uiListen(UiState& s, UiValueFn fn, void* user);
void uiTextStyle(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis, UiTextValign valign, bool wrap, UiTextDeco deco, float leading);
void uiTextReadTail(UiState& s, uint16_t id, const BinTail& tail);
void uiSetDisabled(UiState& s, uint16_t id, bool disabled);
[[nodiscard]] uint16_t uiTextId(const UiState& s, uint16_t id);

inline UiBox uiContent(const UiState& s, uint16_t id) {
    UiBox box = id < s.count ? s.box[id] : UiBox{};
    if (id >= s.count) {
        return box;
    }
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    if (flex == nullptr) {
        return box;
    }
    float left = 0.0f;
    float right = 0.0f;
    float top = 0.0f;
    float bottom = 0.0f;
    uiPadOf(*flex, left, right, top, bottom);
    box.x += left;
    box.y += top;
    box.w -= left + right;
    box.h -= top + bottom;
    if (box.w < 0.0f) {
        box.w = 0.0f;
    }
    if (box.h < 0.0f) {
        box.h = 0.0f;
    }
    return box;
}

template <typename T>
T* uiAncestor(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return nullptr;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (auto* comp = s.ent[p].try_get_mut<T>()) {
            return comp;
        }
    }
    return nullptr;
}

template <typename T>
const T* uiAncestor(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return nullptr;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (const T* comp = s.ent[p].try_get<T>()) {
            return comp;
        }
    }
    return nullptr;
}

template <typename T>
bool uiAncestorHas(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return false;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.ent[p].has<T>()) {
            return true;
        }
    }
    return false;
}

template <typename T>
uint16_t uiAncestorId(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return kUiNone;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.ent[p].try_get<T>() != nullptr) {
            return p;
        }
    }
    return kUiNone;
}

inline bool uiWithin(const UiState& s, uint16_t id, uint16_t ancestor) {
    if (ancestor == kUiNone || id >= s.count) {
        return false;
    }
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == ancestor) {
            return true;
        }
    }
    return false;
}

// True when `id` itself is an absolutely placed node that floats outside normal flow.
inline bool uiOverlayHere(const UiState& s, uint16_t id) {
    const UiFlex* flex = s.ent[id].try_get<UiFlex>();
    if (flex == nullptr || flex->position != UiPosition::Absolute) {
        return false;
    }
    const uint16_t parent = s.parentOf[id];
    if (parent == kUiNone || parent >= s.count) {
        return true;
    }
    const UiFlex* home = s.ent[parent].try_get<UiFlex>();
    return home == nullptr || home->position == UiPosition::Static;
}

inline bool uiIsOverlay(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return false;
    }
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (uiOverlayHere(s, p)) {
            return true;
        }
    }
    return false;
}

} // namespace burnhope

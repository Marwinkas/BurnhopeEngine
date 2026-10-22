#pragma once

#include "platform/Input.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/Metrics.hpp"
#include "ui/Model.hpp"
#include "ui/Text.hpp"
#include "ui/UiBin.hpp"

#include <flecs.h>

typedef struct YGNode* YGNodeRef;

namespace burnhope {

struct UiState;

using UiInputFn = bool (*)(UiState& s, const InputFrame& in);
using UiDrawFn = bool (*)(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiOverlayDrawFn = void (*)(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiTextGateFn = bool (*)(const UiState& s);
using UiDropFn = void (*)(UiState& s);
using UiClearFn = void (*)(UiState& s);
using UiSpawnFn = void (*)(UiState& s, uint16_t id);
using UiBootFn = void (*)(UiState& s);
using UiRoleFinishFn = void (*)(UiState& s, uint16_t id, const BinNode* spec, const char* text, const char* hint);
struct UiNoHit {}; // Пустой тег: объект нематериален для курсора мыши
struct UiPaintLast {}; // Среди братьев рисуется последним, поверх непрозрачных строк.
struct UiIconChrome {}; // Кнопка без плашки: состояние только на цвете иконки.
struct UiMenuSlide {}; // Меню тулбара: высота и альфа при открытии и закрытии.
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
};

// Widget modules register draw hooks. uiEmit never switches on widget paint tags.
struct UiDrawHooks {
    UiDrawFn fn[4]{};
    uint8_t n = 0;
    UiDrawFn decor[8]{};
    uint8_t decorN = 0;
};

struct UiOverlayHooks {
    UiOverlayDrawFn fn[4]{};
    uint8_t n = 0;
};
enum class UiInputPhase : uint8_t {
    Cursor = 1,
    Early = 2,
    Escape = 3,
    Menu = 4,
    Chrome = 5,
    Press = 6,
    PressLate = 7,
    Focus = 8,
    Drag = 9,
    Release = 10,
    Wheel = 11,
    Key = 12,
    Clip = 13,
    Sync = 14
};


using UiInputHookFn = bool (*)(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, uint8_t& cursor);

struct UiInputHooks {
    UiInputHookFn fn[12]{};
    uint8_t n = 0;
};

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
    uint16_t menu = kUiNone;
    uint16_t field = kUiNone;
    uint16_t fieldMenu = kUiNone;
    uint16_t editField = kUiNone;
    UiValueFn valueFn[4]{};
    void* valueUser[4]{};
    uint8_t valueN = 0;
    UiPlug plug{};
    char pasteBuf[96]{};
    uint8_t pasteBufLen = 0;
    uint16_t floatWin = kUiNone;
    uint16_t resizeId = kUiNone;
    uint8_t resizeEdge = 0;
    uint8_t cursor = 0;
    uint8_t culled[kUiCap]{};
    char clip[96]{};
    uint8_t clipLen = 0;
    uint8_t clipPut = 0;
    float grabX = 0;
    float grabY = 0;
    UiClickFn onClick = nullptr;
    void* clickUser = nullptr;
    UiGrabFn grabFn = nullptr;
    void* grabUser = nullptr;
    UiFocusFn focusFn = nullptr;
    void* focusUser = nullptr;
    uint16_t dragId = kUiNone;
    bool menuOpen = false;
    bool opClose = false;
    bool opMinimize = false;
    bool opMaximize = false;
    bool topOn = false;
    bool ghost = false;
    int opTop = -1;
    float opOpacity = -1.0f;
    float time = 0;
    float prevTime = 0;
    bool altLook = false;
    UiLook look{};
    UiMetrics metrics{};
    UiFontAtlas font{};
    UiAnim anim[kUiCap]{};
    UiCommand cmds[kUiCmdCap]{};
    uint8_t cmdCount = 0;
    float laidW = -1.0f;
    float laidH = -1.0f;
    bool layoutDirty = true;
    bool visualDirty = true;
    uint8_t dumpLayout = 0;
    uint16_t layoutFrom = kUiNone;
    UiLayoutFinishHook layoutFinishHook{};
    UiDrop dropBus{};
    char ime[64]{};
    uint8_t imeLen = 0;
    uint8_t relative = 0;
    uint8_t ateWheel = 0;
    float dpi = 1;
    uint8_t clipOn = 0;
    UiBox clipBox{};
    UiCustomBinLoader binHooks[kUiBinRoleCap]{};
    UiClearFn clearFn[4]{};
    uint8_t clearN = 0;
    UiSpawnFn onSpawn[kUiBinRoleCap]{};
    UiRoleFinishFn roleFinish[kUiBinRoleCap]{};
    UiImportBag importBag{};
    void (*beforeLayout)(UiState& s) = nullptr;

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
void uiMarkLayout(UiState& s, uint16_t id);
void uiShowOnly(UiState& s, uint16_t id);
void uiDragBegin(UiState& s, uint16_t source, const char* kind, const char* bytes);
void uiDragEnd(UiState& s, uint16_t target);
void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user);
void uiBindName(UiState& s, const char* name, UiClickFn fn, void* user);
void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user);
void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user);
bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user);
bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user);
void uiShow(UiState& s, uint16_t id, bool visible);
void uiPaintRgb(UiState& s, uint16_t id, float r, float g, float b);
void uiReparent(UiState& s, uint16_t id, uint16_t parent);
void uiDrop(UiState& s, uint16_t id);
void uiTrace(const UiState& s, const char* fn, uint16_t id);
void uiDumpLayout(const UiState& s);
void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h);
void uiGrow(UiState& s, uint16_t id, float extraW, float h);
void uiSetHeight(UiState& s, uint16_t id, float h);
void uiSetDrop(UiState& s, uint16_t id, uint8_t drop);
[[nodiscard]] uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text);
[[nodiscard]] uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiImportBin(UiState& s, uint16_t parent, const void* bytes, uint32_t size);
[[nodiscard]] uint16_t uiImportBinFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiFindName(const UiState& s, const char* name);
inline void uiSetField(UiState& s, uint16_t id) { s.field = id; }
inline void uiSetMenu(UiState& s, uint16_t id) { s.menu = id; }
inline void uiSetFieldMenu(UiState& s, uint16_t id) { s.fieldMenu = id; }
inline uint16_t uiField(const UiState& s) { return s.field; }
inline uint16_t uiMenu(const UiState& s) { return s.menu; }
inline uint16_t uiFieldMenu(const UiState& s) { return s.fieldMenu; }
inline void uiRequestTop(UiState& s, bool on) { s.topOn = on; s.opTop = on ? 1 : 0; }
inline void uiRequestOpacity(UiState& s, float opacity) { s.opOpacity = opacity; s.ghost = opacity < 1.0f; }
inline void uiRequestMinimize(UiState& s) { s.opMinimize = true; }
inline void uiRequestMaximize(UiState& s) { s.opMaximize = true; }
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
void uiName(UiState& s, uint16_t id, const char* name);
void uiDrawAdd(UiState& s, UiDrawFn fn);
void uiDrawDecorAdd(UiState& s, UiDrawFn fn);
void uiOverlayAdd(UiState& s, UiOverlayDrawFn fn);
void uiBootAdd(UiBootFn fn);
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
void uiRevealTick(UiState& s);
[[nodiscard]] bool uiTickMotion(UiState& s);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);

bool uiListen(UiState& s, UiValueFn fn, void* user);
void uiTextStyle(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis, UiTextValign valign, bool wrap, UiTextDeco deco, float leading);
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

inline bool uiIsOverlay(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return false;
    }
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        const UiFlex* flex = s.ent[p].try_get<UiFlex>();
        if (flex == nullptr || flex->position != UiPosition::Absolute) {
            continue;
        }
        const uint16_t parent = s.parentOf[p];
        if (parent == kUiNone || parent >= s.count) {
            return true;
        }
        const UiFlex* home = s.ent[parent].try_get<UiFlex>();
        if (home == nullptr || home->position == UiPosition::Static) {
            return true;
        }
    }
    return false;
}

} // namespace burnhope

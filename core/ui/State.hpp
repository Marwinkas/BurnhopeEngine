#pragma once

#include "platform/Window.hpp"
#include "ui/Layout.hpp"
#include "ui/Model.hpp"

#include <flecs.h>

typedef struct YGNode* YGNodeRef;

namespace burnhope {

struct UiState;

using UiInputFn = bool (*)(UiState& s, const InputFrame& in);
using UiDrawFn = bool (*)(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiOverlayDrawFn = void (*)(UiState& s, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiTextGateFn = bool (*)(const UiState& s);
using UiDropFn = void (*)(UiState& s);

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
};

struct UiState {
    flecs::world world;
    flecs::query<UiSlot, UiPaint> paints;
    YGNodeRef yoga[kUiCap]{};
    flecs::entity ent[kUiCap]{};
    UiBox box[kUiCap]{};
    uint16_t parentOf[kUiCap]{};
    uint16_t labelOf[kUiCap]{};
    uint16_t contextOf[kUiCap]{};
    uint8_t tight[kUiCap]{};
    uint16_t count = 0;
    uint16_t hovered = kUiNone;
    uint8_t cursorStack[8]{};
    uint8_t cursorN = 0;
    uint16_t focused = kUiNone;
    uint16_t capture = kUiNone;
    uint16_t pageLabel = kUiNone;
    uint16_t statusLabel = kUiNone;
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
    uint8_t hintOn = 0;
    uint8_t edgeOn = 0;
    uint8_t culled[kUiCap]{};
    UiBox hint{};
    UiBox edgeLine{};
    float openH[kUiCap]{};
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
    UiFontAtlas font{};
    UiAnim anim[kUiCap]{};
    UiCommand cmds[kUiCmdCap]{};
    uint8_t cmdCount = 0;
    int8_t selected = 0;
    float laidW = -1.0f;
    float laidH = -1.0f;
    bool layoutDirty = true;
    bool visualDirty = true;
    uint8_t dumpLayout = 1;
    uint16_t layoutFrom = kUiNone;
    UiDrop dropBus{};
    char ime[64]{};
    uint8_t imeLen = 0;
    uint8_t relative = 0;
    uint8_t ateWheel = 0;
    float dpi = 1;
    uint8_t clipOn = 0;
    UiBox clipBox{};
    UiCustomBinLoader binHooks[16]{};

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
void uiName(UiState& s, uint16_t id, const char* name);
void uiDrawAdd(UiState& s, UiDrawFn fn);
void uiApplyPhotoMeta(UiState& s, uint8_t slot, uint8_t frames, const uint16_t* delays, uint32_t delayCount);

void uiClear(UiState& s);
uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void uiText(UiState& s, uint16_t id, const char* text);
void uiSetFieldText(UiState& s, uint16_t id, const char* text);
void uiStamp(UiState& s, uint16_t id, UiToken token);
void uiBlur(UiState& s, uint16_t id, float blur);
void uiTransform(UiState& s, uint16_t id, float angle, float scaleX, float scaleY);
void uiFilter(UiState& s, uint16_t id, float bright, float contrast);
void uiSetPaint(UiState& s, uint16_t id, float r, float g, float b, float a);
[[nodiscard]] UiEvent uiApplyInput(UiState& s, const InputFrame& in);
void uiLayout(UiState& s, float w, float h);
void uiRestyle(UiState& s);
void uiMotionTo(UiState& s, uint16_t id, float r, float g, float b, float a);
[[nodiscard]] bool uiTickMotion(UiState& s);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);
[[nodiscard]] uint32_t uiEmitText(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const char* text,
    uint8_t len,
    const UiBox& box,
    float r,
    float g,
    float b,
    const UiBox* clip,
    bool center);

bool uiListen(UiState& s, UiValueFn fn, void* user);
void fieldPlaceText(UiState& s);
[[nodiscard]] TextRun textRun(const UiState& s, const UiText& text, const UiBox& slot);
[[nodiscard]] uint32_t uiEmitRun(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const TextRun& run,
    float r,
    float g,
    float b,
    float a,
    const UiBox* clip);
void uiTextStyle(UiState& s, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading);
[[nodiscard]] uint16_t uiTextId(const UiState& s, uint16_t id);
void textInk(float r, float g, float b, bool hot, float& tr, float& tg, float& tb);

inline float uiAdvance(const UiState& s, uint32_t cp) {
    int gi = uiGlyphIndex(cp);
    if (gi < 0 || gi >= static_cast<int>(kUiFontGlyphs)) {
        gi = static_cast<int>('?' - 32);
    }
    const float scale = s.font.ready && s.font.fontPx > 1.0f ? s.look.glyphH / s.font.fontPx : 1.0f;
    return s.font.ready ? s.font.glyphs[gi].advance * scale : s.look.advance;
}

inline float uiMeasure(const UiState& s, const char* text, uint8_t len, uint8_t until) {
    float w = 0.0f;
    uint8_t i = 0;
    const uint8_t end = until < len ? until : len;
    while (i < end) {
        uint8_t step = 1;
        const uint32_t cp = uiReadUtf8(text, len, i, step);
        if (step == 0) {
            break;
        }
        w += uiAdvance(s, cp);
        i = static_cast<uint8_t>(i + step);
    }
    return w;
}

inline float uiFieldWidth(const UiState& s, const UiField& field, uint8_t until) {
    if (field.password == 0) {
        return uiMeasure(s, field.bytes, field.len, until);
    }
    uint8_t count = 0;
    uint8_t i = 0;
    const uint8_t end = until < field.len ? until : field.len;
    while (i < end) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        ++count;
        i = static_cast<uint8_t>(i + step);
    }
    return static_cast<float>(count) * uiAdvance(s, static_cast<uint32_t>('*'));
}

inline void uiPadOf(const UiFlex& flex, float& left, float& right, float& top, float& bottom) {
    if (flex.padL > 0.0f || flex.padR > 0.0f || flex.padT > 0.0f || flex.padB > 0.0f) {
        left = flex.padL;
        right = flex.padR;
        top = flex.padT;
        bottom = flex.padB;
    } else {
        left = flex.pad;
        right = flex.pad;
        top = flex.pad;
        bottom = flex.pad;
    }
}

// Прямоугольник детей: край рамки Yoga минус padding. Абсолютный left/top считается отсюда.
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

inline bool uiIsOverlay(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        const UiFlex* flex = s.ent[p].try_get<UiFlex>();
        if (flex != nullptr && flex->position != 0) {
            return true;
        }
    }
    return false;
}

} // namespace burnhope

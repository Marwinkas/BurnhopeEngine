#pragma once

#include "platform/Window.hpp"
#include "ui/Model.hpp"

#include <flecs.h>
#include <yoga/Yoga.h>

namespace burnhope {

struct UiState;

using UiInputFn = bool (*)(UiState& s, const InputFrame& in);
using UiDrawFn = bool (*)(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);
using UiTextGateFn = bool (*)(const UiState& s);
using UiDropFn = void (*)(UiState& s);

// A higher module (files, later others) plugs in here. Widgets never name it.
struct UiPlug {
    UiInputFn handle = nullptr;
    UiDrawFn draw = nullptr;
    UiTextGateFn wantsText = nullptr;
    UiDropFn shutdown = nullptr;
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
    uint16_t focused = kUiNone;
    uint16_t capture = kUiNone;
    uint16_t pageLabel = kUiNone;
    uint16_t statusLabel = kUiNone;
    uint16_t pages[3]{};
    uint16_t preview = kUiNone;
    uint16_t listInner = kUiNone;
    uint16_t compactRow = kUiNone;
    uint16_t menu = kUiNone;
    uint16_t field = kUiNone;
    uint16_t fieldMenu = kUiNone;
    uint16_t editField = kUiNone;
    char fileDir[192]{};
    char fileName[16][40]{};
    uint8_t fileIsDir[16]{};
    uint8_t fileCount = 0;
    uint8_t filePick = 0xff;
    uint16_t fileRows[16]{};
    uint16_t filePanel = kUiNone;
    uint16_t fileHead = kUiNone;
    uint16_t fileTarget = kUiNone;
    int calY = 2026;
    int calM = 10;
    int calSel = 0;
    uint16_t calPanel = kUiNone;
    uint16_t calHead = kUiNone;
    uint16_t calDay[42]{};
    uint16_t calTarget = kUiNone;
    float colorR = 0.36f;
    float colorG = 0.55f;
    float colorB = 0.92f;
    float colorH = 0.62f;
    float colorS = 0.61f;
    float colorV = 0.92f;
    float colorA = 1.0f;
    uint16_t colorPanel = kUiNone;
    uint16_t colorSwatch = kUiNone;
    uint16_t colorTarget = kUiNone;
    uint16_t colorWheel = kUiNone;
    uint16_t colorVal = kUiNone;
    uint16_t colorAlpha = kUiNone;
    uint16_t colorHex = kUiNone;
    uint16_t colorRgb[3]{kUiNone, kUiNone, kUiNone};
    uint16_t colorHsv[3]{kUiNone, kUiNone, kUiNone};
    uint16_t colorNum[7]{kUiNone, kUiNone, kUiNone, kUiNone, kUiNone, kUiNone, kUiNone};
    uint16_t colorAnchor = kUiNone;
    uint16_t colorAlphaBtn = kUiNone;
    uint16_t colorMenuBtn = kUiNone;
    uint16_t colorWinBtn = kUiNone;
    ColorBook* book = nullptr;
    uint32_t colorSeen = 0;
    uint8_t colorUseAlpha = 1;
    uint8_t colorMode = 0;
    uint8_t colorPresent = 0;
    uint8_t colorApplied = 0;
    uint8_t colorHost = 0;
    uint8_t colorDrop = 0;
    uint16_t formNote = kUiNone;
    uint16_t photoView = kUiNone;
    uint16_t fileView = kUiNone;
    uint16_t fileFrame = kUiNone;
    UiValueFn valueFn[4]{};
    void* valueUser[4]{};
    uint8_t valueN = 0;
    UiPlug plug{};
    void* files = nullptr;
    char* jsonArena = nullptr;
    uint32_t jsonUsed = 0;
    uint32_t jsonCap = 0;
    flecs::entity jsonDoc{};
    char pasteBuf[96]{};
    uint8_t pasteBufLen = 0;
    uint16_t sideId = kUiNone;
    uint16_t openedMenu = kUiNone;
    uint16_t floatWin = kUiNone;
    uint16_t dockHome = kUiNone;
    uint16_t dockPanel = kUiNone;
    uint16_t resizeId = kUiNone;
    uint8_t dockEdges = 15;
    uint8_t dockEdge = 0;
    uint8_t resizeEdge = 0;
    uint8_t cursor = 0;
    uint8_t hintOn = 0;
    uint8_t edgeOn = 0;
    uint8_t culled[kUiCap]{};
    UiBox hint{};
    UiBox edgeLine{};
    bool opTool = false;
    bool opColor = false;
    bool opFile = false;
    float openH[kUiCap]{};
    char clip[96]{};
    uint8_t clipLen = 0;
    uint8_t clipPut = 0;
    uint16_t pickedId = kUiNone;
    char pickedName[32]{};
    uint8_t pickedLen = 0;
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
    float menuW = 180.0f;
    float menuItemH = 28.0f;
    float menuPad = 6.0f;
    float menuGap = 4.0f;
    float time = 0;
    float prevTime = 0;
    bool altLook = false;
    UiLook look{};
    float glyphU0[kUiFontGlyphs]{};
    float glyphV0[kUiFontGlyphs]{};
    float glyphU1[kUiFontGlyphs]{};
    float glyphV1[kUiFontGlyphs]{};
    float glyphBx[kUiFontGlyphs]{};
    float glyphBy[kUiFontGlyphs]{};
    float glyphW[kUiFontGlyphs]{};
    float glyphH[kUiFontGlyphs]{};
    float glyphAdv[kUiFontGlyphs]{};
    float fontPx = 32.0f;
    bool fontReady = false;
    UiAnim anim[kUiCap]{};
    UiCommand cmds[kUiCmdCap]{};
    uint8_t cmdCount = 0;
    int8_t selected = 0;
    float laidW = -1.0f;
    float laidH = -1.0f;
    bool layoutDirty = true;
    bool visualDirty = true;

    UiState();
};

void uiFocus(UiState& s, uint16_t id);
void uiBind(UiState& s, uint16_t id, UiClickFn fn, void* user);
void uiDrag(UiState& s, uint16_t id, UiDragFn fn, void* user);
void uiEdit(UiState& s, uint16_t id, UiEditFn fn, void* user);
void uiFold(UiState& s, uint16_t id);
bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user);
bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user);
void uiShow(UiState& s, uint16_t id, bool visible);
void uiReparent(UiState& s, uint16_t id, uint16_t parent);
void uiPlace(UiState& s, uint16_t id, bool absolute, float x, float y, float w, float h);
void uiGrow(UiState& s, uint16_t id, float extraW, float h);
void uiDockTo(UiState& s, uint8_t edge);
void uiBuildShell(UiState& s);
void uiBuildColor(UiState& s);
[[nodiscard]] uint16_t uiImportJson(UiState& s, uint16_t parent, const char* text);
[[nodiscard]] uint16_t uiImportJsonFile(UiState& s, uint16_t parent, const char* path);
[[nodiscard]] uint16_t uiFindName(const UiState& s, const char* name);

// General JSON document. Nodes live in Flecs; string bytes live in UiState::jsonArena.
enum class JsonKind : uint8_t { Null = 0, Bool = 1, Number = 2, String = 3, Array = 4, Object = 5 };

struct JsonNode {
    uint64_t parent = 0;
    uint64_t first = 0;
    uint64_t next = 0;
    uint16_t order = 0;
    uint8_t kind = 0;
    uint8_t boolean = 0;
    double number = 0;
    uint32_t textOff = 0;
    uint32_t textLen = 0;
    char key[48]{};
};

void jsonRelease(UiState& s);
[[nodiscard]] uint32_t jsonLoadFile(UiState& s, const char* path);
[[nodiscard]] bool jsonSaveFile(const UiState& s, const char* path);
[[nodiscard]] flecs::entity jsonFind(flecs::entity parent, const char* key);
[[nodiscard]] flecs::entity jsonAt(const UiState& s, flecs::entity parent, uint32_t index);
[[nodiscard]] bool jsonString(const UiState& s, flecs::entity e, const char*& data, uint32_t& len);
[[nodiscard]] bool jsonNumber(flecs::entity e, double& out);
void uiClear(UiState& s);
uint16_t uiNode(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void uiText(UiState& s, uint16_t id, const char* text);
void uiSetFieldText(UiState& s, uint16_t id, const char* text);
void uiStamp(UiState& s, uint16_t id, UiToken token);
void uiShowPage(UiState& s, int page);
void uiToggleCompact(UiState& s);
void uiClearField(UiState& s);
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
void placeChromeText(UiState& s);
void uiTextStyle(UiState& s, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading);
[[nodiscard]] uint16_t uiTextId(const UiState& s, uint16_t id);
[[nodiscard]] uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h);
[[nodiscard]] uint16_t uiTip(UiState& s, uint16_t parent);
void uiTipAt(UiState& s, uint16_t id, const char* text, float x, float y);
[[nodiscard]] bool scrollThumb(const UiState& s, uint16_t id, UiBox& thumb);
void dragScroll(UiState& s, uint16_t id, float y);
void textInk(float r, float g, float b, bool hot, float& tr, float& tg, float& tb);
[[nodiscard]] UiBox fieldInset(const UiState& s, uint16_t id, const UiField& field);

inline float uiAdvance(const UiState& s, uint32_t cp) {
    int gi = uiGlyphIndex(cp);
    if (gi < 0 || gi >= static_cast<int>(kUiFontGlyphs)) {
        gi = static_cast<int>('?' - 32);
    }
    const float scale = s.fontReady && s.fontPx > 1.0f ? s.look.glyphH / s.fontPx : 1.0f;
    return s.fontReady ? s.glyphAdv[gi] * scale : s.look.advance;
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

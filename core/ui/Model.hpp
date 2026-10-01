#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint16_t kUiCap = 1024;
constexpr uint16_t kUiNone = 0xffff;
constexpr uint32_t kUiPrimCap = 2048;
constexpr uint32_t kUiHeaderBytes = 64;
constexpr uint32_t kUiPrimBytes = 64;
constexpr uint32_t kUiFontGlyphs = 192;
constexpr uint32_t kUiGlyphStride = 16;
constexpr uint32_t kUiFontBytes = kUiFontGlyphs * kUiGlyphStride;
constexpr uint32_t kUiFontBase = kUiHeaderBytes + kUiPrimCap * kUiPrimBytes;

enum class UiRole : uint8_t {
    Panel = 0,
    Button = 1,
    Label = 2,
    Slider = 3,
    Check = 4,
    Field = 5,
    Close = 6,
    Scroll = 7,
    Hidden = 8,
    Progress = 9,
};

enum class UiSize : uint8_t { Auto = 0, Px = 1, Percent = 2 };

// 0 = цвет задан вручную и тема его не трогает.
enum class UiToken : uint8_t {
    Custom = 0,
    Window,
    Title,
    Side,
    Content,
    Button,
    Field,
    Menu,
    Close,
    Text,
    Muted,
    Item,
    Preview,
    Status,
    Slider,
    Check,
    Scroll,
    Chrome,
    Accent,
};

enum class UiEvent : uint8_t { None = 0, Close = 1 };

constexpr uint8_t kUiCmdCap = 48;

struct WindowOps {
    bool close = false;
    bool minimize = false;
    bool toggleMaximize = false;
    bool openTool = false;
    bool openColor = false;
    bool openFile = false;
    int alwaysOnTop = -1;
    float opacity = -1.0f;
};

// Общий цвет между холстом и отдельным окном выбора.
struct ColorBook {
    float r = 0.36f;
    float g = 0.55f;
    float b = 0.92f;
    float a = 1.0f;
    float h = 0.62f;
    float s = 0.61f;
    float v = 0.92f;
    uint8_t useAlpha = 1;
    uint8_t mode = 0;    // 0 меню у кнопки, 1 отдельное окно
    uint8_t present = 0; // 0 закрыт, 1 меню, 2 окно
    uint8_t drop = 0;    // 1 пипетка ждёт клик по окну
    uint32_t gen = 0;
};

struct UiName {
    char bytes[32]{};
    uint8_t len = 0;
};

using UiCommandFn = void (*)(void* user);
using UiClickFn = void (*)(void* user, uint16_t id, int16_t tag);
using UiValueFn = void (*)(void* user, uint16_t id, float value);
using UiDragFn = void (*)(void* user, uint16_t id, float x, float y);
using UiEditFn = void (*)(void* user, uint16_t id);
using UiGrabFn = bool (*)(void* user, uint16_t hit, float x, float y);
using UiFocusFn = void (*)(void* user, uint16_t id);

// Функция, которую задали снаружи, когда создавали виджет. Сам виджет её не знает.
struct UiAction {
    UiClickFn fn = nullptr;
    void* user = nullptr;
};

struct UiDrag {
    UiDragFn fn = nullptr;
    void* user = nullptr;
};

struct UiEdit {
    UiEditFn fn = nullptr;
    void* user = nullptr;
};

struct UiCommand {
    char name[24]{};
    uint8_t len = 0;
    UiCommandFn fn = nullptr;
    void* user = nullptr;
};

// Flecs columns. Fixed buffers, no heap inside a component.
struct UiSlot {
    uint16_t index = 0;
};

struct UiFlex {
    uint8_t direction = 0; // 0 column, 1 row, 2 column-reverse, 3 row-reverse
    uint8_t align = 0;     // align-items: 0 stretch, 1 center, 2 start, 3 end, 4 baseline
    uint8_t justify = 0;   // 0 start, 1 center, 2 end, 3 between, 4 around, 5 evenly
    uint8_t widthMode = 0;
    uint8_t heightMode = 0;
    float width = 0;
    float height = 0;
    float grow = 0;
    float shrink = 1;
    float pad = 0;
    float padL = 0;
    float padR = 0;
    float padT = 0;
    float padB = 0;
    float gap = 0;         // column-gap; если gapRow < 0, ещё и row-gap
    float minW = 0;
    float minH = 0;
    float marginL = 0;
    float marginR = 0;
    float marginT = 0;
    float marginB = 0;
    uint8_t position = 0; // 0 static, 1 absolute, 2 relative
    uint8_t drop = 0;     // 0 off, 1 hide the last child that does not fit, 2 hide the first
    float posX = 0;
    float posY = 0;
    uint8_t wrap = 0;      // 0 nowrap, 1 wrap, 2 wrap-reverse
    uint8_t content = 0;   // align-content: 0 stretch, 1 center, 2 start, 3 end, 4 between, 5 around, 6 evenly
    uint8_t self = 0;      // align-self: 0 auto, 1 stretch, 2 center, 3 start, 4 end, 5 baseline
    uint8_t basisMode = 0; // 0 auto, 1 px, 2 percent
    uint8_t maxWMode = 0;  // 0 нет, 1 px, 2 percent
    uint8_t maxHMode = 0;
    uint8_t overflow = 0;  // 0 visible, 1 hidden, 2 scroll
    uint8_t box = 0;       // 0 border-box, 1 content-box
    uint8_t inset = 0;     // бит 0 — right, бит 1 — bottom
    float basis = 0;
    float maxW = 0;
    float maxH = 0;
    float gapRow = -1;
    float aspect = 0;      // 0 нет
    float posR = 0;
    float posB = 0;
};

struct UiPaint {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;
    float radius = 0;
    float borderW = 0;
    float br = 0.45f;
    float bg = 0.50f;
    float bb = 0.62f;
    uint8_t role = 0;
    uint8_t token = 0;
    uint8_t icon = 0;
    uint8_t ramp = 0;
    uint8_t disabled = 0;
    float shadow = 0;
    int16_t tag = -1;
    float hr = -1;
    float hg = -1;
    float hb = -1;
};

// Один POD на весь кадр. Меняет цвета, радиусы, шрифт, длительность анимации и хром окна.
struct UiLook {
    float window[3]{0.102f, 0.106f, 0.122f};
    float title[3]{0.145f, 0.153f, 0.176f};
    float side[3]{0.118f, 0.125f, 0.145f};
    float content[3]{0.086f, 0.090f, 0.106f};
    float button[3]{0.20f, 0.22f, 0.28f};
    float field[3]{0.14f, 0.15f, 0.19f};
    float menu[3]{0.11f, 0.12f, 0.15f};
    float close[3]{0.55f, 0.28f, 0.28f};
    float text[3]{0.93f, 0.94f, 0.96f};
    float muted[3]{0.62f, 0.66f, 0.74f};
    float item[3]{0.16f, 0.17f, 0.21f};
    float preview[3]{0.36f, 0.42f, 0.55f};
    float status[3]{0.078f, 0.082f, 0.098f};
    float slider[3]{0.18f, 0.20f, 0.26f};
    float check[3]{0.16f, 0.17f, 0.21f};
    float chrome[3]{0.22f, 0.24f, 0.28f};
    float accent[3][3]{
        {0.36f, 0.55f, 0.92f},
        {0.45f, 0.72f, 0.48f},
        {0.86f, 0.55f, 0.32f},
    };
    float radius = 8.0f;
    float radiusSm = 6.0f;
    float motion = 0.14f;
    float hover = 0.08f;
    float titleBar = 40.0f;
    float resizeBorder = 6.0f;
    float advance = 9.0f;
    float glyphW = 8.0f;
    float glyphH = 18.0f;
};

struct UiAnim {
    float fr = 0;
    float fg = 0;
    float fb = 0;
    float fa = 1;
    float tr = 0;
    float tg = 0;
    float tb = 0;
    float ta = 1;
    float t0 = 0;
    float dur = 0;
    uint8_t on = 0;
};

inline UiLook uiLookWarm() {
    UiLook look{};
    look.window[0] = 0.16f;
    look.window[1] = 0.10f;
    look.window[2] = 0.08f;
    look.title[0] = 0.22f;
    look.title[1] = 0.13f;
    look.title[2] = 0.10f;
    look.side[0] = 0.20f;
    look.side[1] = 0.12f;
    look.side[2] = 0.09f;
    look.content[0] = 0.12f;
    look.content[1] = 0.08f;
    look.content[2] = 0.07f;
    look.accent[0][0] = 0.93f;
    look.accent[0][1] = 0.55f;
    look.accent[0][2] = 0.28f;
    look.accent[1][0] = 0.95f;
    look.accent[1][1] = 0.72f;
    look.accent[1][2] = 0.35f;
    look.accent[2][0] = 0.86f;
    look.accent[2][1] = 0.40f;
    look.accent[2][2] = 0.32f;
    look.motion = 0.22f;
    look.radius = 14.0f;
    look.radiusSm = 10.0f;
    return look;
}

struct UiText {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t align = 0;     // 0 left, 1 center, 2 right, 3 justify
    uint8_t ellipsis = 0;  // 0 нет, 1 конец, 2 середина
    uint8_t valign = 1;    // 0 top, 1 center, 2 bottom
    uint8_t wrap = 0;      // 1 перенос
    uint8_t deco = 0;      // бит 0 underline, 1 strike, 2 overline
    uint8_t transform = 0; // 0 нет, 1 upper, 2 lower, 3 capitalize
    uint8_t breakAll = 0;  // 1 — разрыв внутри слова
    uint8_t tabs = 0;      // 0 значит 4 пробела на таб
    float leading = 0;
    float scroll = 0;
    float tracking = 0; // letter-spacing, px
    float wordGap = 0;  // word-spacing, px сверх пробела
    float indent = 0;   // text-indent первой строки, px
    float size = 0;     // множитель em, 0 значит 1
    float thick = 0;    // толщина линии декора, 0 значит 1
    float under = 0;    // сдвиг подчёркивания вниз, px
    float shadowX = 0;
    float shadowY = 0;
};

struct UiField {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t caret = 0;
    uint8_t anchor = 0;
    uint8_t filter = 0; // 0 текст, 1 цифры, 2 буквы, 3 email, 4 tel, 5 url
    uint8_t clear = 0;
    uint8_t maxChars = 0;
    uint8_t readOnly = 0;
    uint8_t required = 0;
    uint8_t password = 0;
    uint8_t inForm = 0;
    char hint[40]{};
    uint8_t hintLen = 0;
    float scroll = 0;
    float text[3]{0.93f, 0.94f, 0.96f};
    float padL = 10;
    float padR = 10;
};

struct UiRange {
    float value = 0;
    float min = 0;
    float max = 1;
};

struct UiCheck {
    uint8_t on = 0;
    uint8_t kind = 0; // 0 checkbox, 1 radio
    uint8_t group = 0;
};

struct UiScroll {
    float offset = 0;
    uint16_t inner = kUiNone;
};

constexpr uint8_t kPhotoSlots = 8;

// One GPU slot. GIF frames are stacked in that texture; view is a window inside the current frame.
struct UiImage {
    uint8_t slot = 0;
    uint8_t frames = 1;
    uint8_t frame = 0;
    uint8_t minimap = 1;
    uint16_t delayMs[16]{};
    float acc = 0;
    float viewX = 0;
    float viewY = 0;
    float viewW = 1;
    float viewH = 1;
};

struct UiBox {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

constexpr uint8_t kUiLineCap = 8;

struct TextLine {
    uint8_t begin = 0;
    uint8_t len = 0;
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
    float gap = 0; // лишние px на каждый пробел строки (justify)
};

struct TextRun {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t lines = 0;
    uint8_t deco = 0;
    float scale = 1;
    float tracking = 0;
    float wordGap = 0;
    float thick = 1;
    float under = 0;
    float shadowX = 0;
    float shadowY = 0;
    TextLine line[kUiLineCap]{};
    UiBox box{};
};

// std430: extra starts at 48. Clip lives in extra.yzw + pad0.
struct UiPrimitive {
    float posX = 0;
    float posY = 0;
    float sizeX = 0;
    float sizeY = 0;
    float color[4]{};
    uint32_t texId = 0;
    uint32_t flags = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
    float extra[4]{};
};

static_assert(std::is_trivially_copyable_v<UiSlot>);
static_assert(std::is_trivially_copyable_v<UiFlex>);
static_assert(std::is_trivially_copyable_v<UiPaint>);
static_assert(std::is_trivially_copyable_v<UiLook>);
static_assert(std::is_trivially_copyable_v<UiAnim>);
static_assert(std::is_trivially_copyable_v<UiText>);
static_assert(std::is_trivially_copyable_v<UiField>);
static_assert(std::is_trivially_copyable_v<UiRange>);
static_assert(std::is_trivially_copyable_v<UiCheck>);
static_assert(std::is_trivially_copyable_v<UiScroll>);
static_assert(std::is_trivially_copyable_v<UiImage>);
static_assert(std::is_trivially_copyable_v<UiCommand>);
static_assert(std::is_trivially_copyable_v<UiPrimitive>);
static_assert(sizeof(UiPrimitive) == kUiPrimBytes);
static_assert(offsetof(UiPrimitive, extra) == 48);

inline int uiGlyphIndex(uint32_t cp) {
    if (cp >= 32 && cp <= 126) {
        return static_cast<int>(cp - 32);
    }
    if (cp == 0x0401) {
        return 95;
    }
    if (cp >= 0x0410 && cp <= 0x044F) {
        return 96 + static_cast<int>(cp - 0x0410);
    }
    if (cp == 0x0451) {
        return 160;
    }
    return -1;
}

inline uint32_t uiReadUtf8(const char* s, uint8_t len, uint8_t at, uint8_t& step) {
    step = 1;
    if (s == nullptr || at >= len) {
        step = 0;
        return 0;
    }
    const unsigned char c0 = static_cast<unsigned char>(s[at]);
    if (c0 < 0x80) {
        return c0;
    }
    if ((c0 & 0xE0) == 0xC0 && static_cast<uint8_t>(at + 1) < len) {
        step = 2;
        return (static_cast<uint32_t>(c0 & 0x1F) << 6) | (static_cast<unsigned char>(s[at + 1]) & 0x3Fu);
    }
    if ((c0 & 0xF0) == 0xE0 && static_cast<uint8_t>(at + 2) < len) {
        step = 3;
        return (static_cast<uint32_t>(c0 & 0x0F) << 12)
            | (static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1]) & 0x3F) << 6)
            | (static_cast<unsigned char>(s[at + 2]) & 0x3Fu);
    }
    return c0;
}

constexpr uint32_t kUiFlagGlyph = 1;
constexpr uint32_t kUiFlagClip = 2;
constexpr uint32_t kUiFlagIcon = 4;
constexpr uint32_t kUiFlagWheel = 8;
constexpr uint32_t kUiFlagRamp = 16;
constexpr uint32_t kUiFlagPhoto = 32;
constexpr uint32_t kUiFlagStroke = 64;
constexpr uint32_t kUiFlagShadow = 128;

// Colors and chrome of the file browser. The browser does not know about the app.
struct FileLook {
    float icon = 84.0f;
    float tree = 188.0f;
    float details = 210.0f;
    uint8_t showTree = 1;
    uint8_t showDetails = 1;
    uint8_t showHidden = 0;
    float side[3]{0.11f, 0.122f, 0.145f};
    float grid[3]{0.07f, 0.075f, 0.09f};
    float card[3]{0.15f, 0.16f, 0.19f};
    float accent[3]{0.28f, 0.48f, 0.92f};
    float folder[3]{0.91f, 0.72f, 0.29f};
};

using FileOpenFn = void (*)(void* user, const char* path, int isDir);

} // namespace burnhope

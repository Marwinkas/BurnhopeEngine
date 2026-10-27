#pragma once

#include <cstdint>
#include <string_view>

#include "gui/data/GuiComponents.hpp" // kGuiNoClass
#include "gui/text/GuiTextEngine.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiStyleCap = 256;
constexpr uint32_t kGuiStyleNameCap = 32;
constexpr uint32_t kGuiPaletteCap = 64;
constexpr uint32_t kGuiStyleTokenCap = 64; // named numeric constants of the "tokens" block

// Palette ids stored in UiPaint::token / borderToken. Values mirror the legacy UiToken order so the same ids
// mean the same colors; 0 (Custom) is never themed and resolves to white so UiPaint::tint carries the color.
enum GuiTokens : uint8_t {
    kTokCustom = 0,
    kTokWindow,
    kTokTitle,
    kTokSide,
    kTokContent,
    kTokButton,
    kTokField,
    kTokMenu,
    kTokClose,
    kTokText,
    kTokMuted,
    kTokItem,
    kTokPreview,
    kTokStatus,
    kTokSlider,
    kTokCheck,
    kTokScroll,
    kTokChrome,
    kTokAccent,
    kTokAccent1,
    kTokAccent2,
    kTokToolActive,
    kTokAxisX,
    kTokAxisY,
    kTokAxisZ,
    kTokTool,
    kTokToolHover,
    kTokCount,
};

enum class GuiFlexDir : uint8_t { Column = 0, Row, ColumnReverse, RowReverse };
enum class GuiAlign : uint8_t { Auto = 0, Start, Center, End, Stretch, Baseline };
enum class GuiJustify : uint8_t { Start = 0, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class GuiWrap : uint8_t { NoWrap = 0, Wrap, WrapReverse };
enum class GuiOverflow : uint8_t { Visible = 0, Hidden, Scroll };
enum class GuiPosition : uint8_t { Relative = 0, Absolute };

// Bit index of each field in GuiStyle::set. A field is applied only when its bit is set.
enum GuiStyleBit : uint8_t {
    kSbW = 0, kSbH, kSbMinW, kSbMinH, kSbMaxW, kSbMaxH,
    kSbWPercent, kSbHPercent, // w / h are percentages of the parent instead of pixels
    kSbWAuto, kSbHAuto,
    kSbPadL, kSbPadT, kSbPadR, kSbPadB,
    kSbMarL, kSbMarT, kSbMarR, kSbMarB,
    kSbGap,
    kSbPosL, kSbPosT, kSbPosR, kSbPosB,
    kSbFlexDir, kSbGrow, kSbShrink, kSbAlignItems, kSbAlignSelf, kSbJustify, kSbWrap, kSbOverflow, kSbPosition,
    kSbToken, kSbBorderToken, kSbActiveToken, kSbBorderW, kSbRadius, kSbAlpha, kSbShadow,
    kSbIconSize, kSbTextSize, kSbTextAlign, kSbHoverAlpha,
    kSbCount,
};
static_assert(kSbCount <= 64);

// Radius sentinels resolved against the sheet's theme radii (JSON "$theme.radius" / "$theme.radiusSm").
constexpr float kGuiRadiusTheme = -2.0f;
constexpr float kGuiRadiusThemeSm = -3.0f;

// One resolved style class (extends already folded in). POD, 16-byte multiple.
struct alignas(16) GuiStyle {
    float w = 0.0f, h = 0.0f, minW = 0.0f, minH = 0.0f, maxW = 0.0f, maxH = 0.0f;
    float pad[4] = {};    // L, T, R, B
    float margin[4] = {}; // L, T, R, B
    float pos[4] = {};    // absolute offsets L, T, R, B
    float gap = 0.0f;
    float grow = 0.0f;
    float shrink = 1.0f;
    float borderW = 0.0f;
    float radius = 0.0f; // or kGuiRadiusTheme / kGuiRadiusThemeSm
    float alpha = 1.0f;  // panel alpha, multiplied into UiPaint::tint[3]
    float shadow = 0.0f;
    float iconSize = 0.0f;
    float hoverAlpha = 0.0f;
    uint8_t flexDir = 0, alignItems = 0, alignSelf = 0, justify = 0, wrap = 0, overflow = 0, position = 0;
    uint8_t token = 0, borderToken = 0, activeToken = 0;
    uint8_t textSize = 0, textAlign = 0;
    uint64_t set = 0;
};

[[nodiscard]] constexpr bool guiStyleHas(const GuiStyle& s, GuiStyleBit bit) noexcept {
    return (s.set >> bit & 1u) != 0u;
}

// All classes of a styles.json plus the palette and the text metrics. Large (~70 KB): keep one per context, never
// copy it by value on the stack. Zero-initialised storage is valid; guiStyleInit is run lazily by the loaders.
struct GuiStyleSheet {
    GuiStyle cls[kGuiStyleCap] = {};
    char names[kGuiStyleCap][kGuiStyleNameCap] = {};
    uint32_t hashes[kGuiStyleCap] = {};
    uint32_t count = 0;
    float colors[kGuiPaletteCap][4] = {};
    float themeRadius = 8.0f;
    float themeRadiusSm = 6.0f;
    float hover = 0.08f; // default hover lift for classes without "hoverAlpha"
    uint8_t ready = 0;   // set by guiStyleInit
    uint32_t constCount = 0;
    char constName[kGuiStyleTokenCap][24] = {}; // the "tokens" block ("tile.gutter"), kept for guiStyleNumber
    float constValue[kGuiStyleTokenCap] = {};
    GuiTextMetrics text = {};
};

// Resets the sheet: no classes, the default (legacy) palette and analytic text metrics.
void guiStyleInit(GuiStyleSheet& sheet);

// Parses the assets/ui/styles.json format: {"tokens": {...}, "classes": {"name": {...}}}. Values are numbers, "$token" /
// "$group.token" references, "$theme.radius", "$theme.radiusSm", "auto" or "NN%" (w/h). "extends" copies an earlier class.
// Existing classes are replaced; a failed parse leaves the sheet empty (count 0) but valid. Unknown keys are skipped.
bool guiStyleLoad(GuiStyleSheet& sheet, std::string_view json);

// Same, but a class that already exists keeps every key the document omits (overlay on a loaded sheet).
bool guiStyleLoadOver(GuiStyleSheet& sheet, std::string_view json);

// Maps the file read-only (fileMapReadOnly) and parses it with guiStyleLoad.
bool guiStyleLoadFile(GuiStyleSheet& sheet, const char* path);

// Applies one theme of an assets/ui/theme.json document ({"themes": {"name": {"window": [r,g,b], ...}}}) onto the palette.
// The palette is reset to the default first. Returns false when the document or the theme name is missing.
bool guiStyleLoadTheme(GuiStyleSheet& sheet, std::string_view json, std::string_view theme);

// Numeric constant of the "tokens" block by its dotted name ("tile.gutter"); false when the sheet has no such token.
// Application code reads its sizes from here instead of hard-coding them.
bool guiStyleNumber(const GuiStyleSheet& sheet, std::string_view name, float& out);

// Class id of a name, or kGuiNoClass (0xFFFF, see GuiComponents.hpp) when absent. Build-time only (hash scan).
uint32_t guiStyleFind(const GuiStyleSheet& sheet, std::string_view name);

// Style of a class, or nullptr for kGuiNoClass / out of range.
const GuiStyle* guiStyleGet(const GuiStyleSheet& sheet, uint32_t classId);

// Class radius with the theme sentinels resolved.
float guiStyleRadius(const GuiStyleSheet& sheet, const GuiStyle& style);

// Hover lift of a class: its "hoverAlpha", else the sheet default. kGuiNoClass gives 0.
float guiStyleHoverAlpha(const GuiStyleSheet& sheet, uint32_t classId);

// Palette color of a token as rgba (alpha 1). Unknown tokens resolve to token 0 (white).
void guiStyleColor(const GuiStyleSheet& sheet, uint8_t token, float rgba[4]);

} // namespace burnhope::gui

#pragma once

#include "ui/Style.hpp"
#include "ui/Types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace burnhope {

// Numeric properties a style class can set. assets/ui/styles.json is the only place their values live: a key a class
// does not list reads as an error (BH_ASSERT in debug, 0 in release), so widgets never carry a fallback of their own.
enum class UiStyleKey : uint8_t {
    Height,
    Inner,
    Width,
    MaxW,
    MinW,
    Pad,
    PadL,
    PadR,
    Gap,
    Border,
    Radius,
    Icon,
    Alpha,
    Trail,
    LabelW,
    FieldW,
    BadgeW,
    LockGap,
    HoverAlpha,
    HoverGain,
    Chrome,
    RowH,
    ErrorBorder,
    ClearIcon,
    LabelPad,
    IconSide,
    IconInset,
    IconFitMin,
    ShadowDy,
    ShadowAlpha,
    ShadowGrow,
    DisabledAlpha,
    HoverLift,
    HoverFallback,
    HoverMinAlpha,
    Plate,
    Margin,
    MaxH,
    MinH,
    CaretW,
    SelectionAlpha,
    TrackH,
    ThumbSide,
    HandleSide,
    IconFitPad,
    IconMin,
    IconMax,
    InkThreshold,
    Shadow,
    ToolIcon,
    BorderDim,
    ClearW,
    IconLeft,
    MarginR,
    Offset,
    AnchorGap,
    ScreenGap,
    FlyoutW,
    PopupMinW,
    MinThumb,
    WheelStep,
    BarInset,
    BasePane,
    HitSlop,
    MinPane,
    HotAlpha,
    IdleAlpha,
    BarMaxH,
    Chevron,
    TreeIndent,
    TileGap,
    GridInset,
    LabelH,
    SubH,
    SubScale,
    ListPad,
    ListGap,
    SelectedA,
    HeaderA,
    DragSlop,
    OffsetY,
    GlyphW,
    RowInset,
    Ramp,
    Count,
};

static_assert(static_cast<size_t>(UiStyleKey::Count) <= 128, "UiKeyMask holds 128 keys");

// The keys a class sets, one bit per UiStyleKey.
struct UiKeyMask {
    uint64_t word[2]{};
    void set(UiStyleKey key) { word[index(key) >> 6] |= uint64_t{1} << (index(key) & 63u); }
    [[nodiscard]] bool test(UiStyleKey key) const { return ((word[index(key) >> 6] >> (index(key) & 63u)) & 1u) != 0; }
    void merge(const UiKeyMask& other) {
        word[0] |= other.word[0];
        word[1] |= other.word[1];
    }
    bool operator==(const UiKeyMask&) const = default;

private:
    static constexpr unsigned index(UiStyleKey key) { return static_cast<unsigned>(key); }
};

constexpr uint8_t kUiStyleCap = 192;
// A class "radius" written "$theme.radius" / "$theme.radiusSm" follows the active palette (UiLook::radius / radiusSm).
constexpr float kUiRadiusTheme = -2.0f;
constexpr float kUiRadiusThemeSm = -3.0f;
constexpr uint8_t kUiStyleTokenCap = 64;

struct UiStyleClass {
    char name[24]{};
    uint32_t hash = 0;
    float value[static_cast<size_t>(UiStyleKey::Count)]{};
    UiKeyMask has{};
    UiToken bg = UiToken::Custom;
    UiToken activeBg = UiToken::Custom; // token while the node is active (uiSetActive)
    UiToken borderBg = UiToken::Custom; // token the border is drawn in
};

// Classes sorted by name hash so a lookup is a binary search.
struct UiStyleSheet {
    UiStyleClass cls[kUiStyleCap]{};
    uint8_t count = 0;
};

// Replaces the sheet with the classes in `json` and applies the "window_panel" class to the popup look. Layout:
//   {"tokens": {"space": {"md": 8}, "ctl_h": 26},
//    "classes": {"name": {"extends": "other", "h": "$ctl_h", "gap": "$space.md", "border": 1, ...}}}
// A number may be written "$group.name" or "$name" to read a token; "extends" must name an earlier class.
// Returns false (sheet untouched) when the text does not parse, names an unknown token or class, or has
// more than kUiStyleCap classes.
bool uiStyleLoad(UiState& s, std::string_view json);
bool uiStyleLoadAsset(UiState& s, const char* asset);
// Like uiStyleLoad, but the classes in `json` are laid over the loaded sheet: a class it names keeps every key the
// document leaves out, and the other classes stay. For tweaks (tests, tools); F5 reloads the whole asset instead.
bool uiStyleLoadOver(UiState& s, std::string_view json);

// The class as the loaded sheet defines it; nullptr without a sheet or when the sheet lacks the class.
[[nodiscard]] const UiStyleClass* uiStyleFind(const UiState& s, const char* name);
[[nodiscard]] bool uiStyleHas(const UiStyleClass* cls, UiStyleKey key);
// The class as the loaded sheet defines it. False when the sheet lacks the class.
[[nodiscard]] bool uiStyleResolve(const UiState& s, const char* name, UiStyleClass& out);
// The "bg" token of the class (Custom when it has none).
[[nodiscard]] UiToken uiStyleBg(const UiState& s, const char* name);
// The sheet's value. A class or key the sheet lacks is a bug: BH_ASSERT in debug, 0 in release.
[[nodiscard]] float uiStyle(const UiState& s, const char* cls, UiStyleKey key);

// JSON spelling of a key ("h", "labelW", ...).
[[nodiscard]] const char* uiStyleKeyName(UiStyleKey key);

} // namespace burnhope

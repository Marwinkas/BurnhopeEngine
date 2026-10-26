#pragma once

#include "ui/Geometry.hpp"
#include <cstdint>
#include <type_traits>

namespace burnhope {

// Семантические токены оформления. Никаких сырых RGB в коде виджетов!
enum class UiToken : uint8_t {
    Custom = 0, // Цвет задан вручную, тема его не меняет
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

enum class UiRamp : uint8_t {
    None = 0,
    HueWheel = 1,
    ValueRamp = 2,
    Saturation = 3,
    Gradient = 4,
    Alpha = 5,
};

// Height of every dropdown trigger (menu button, choice).
constexpr float kUiTriggerH = 26.0f;

// Визуальный компонент сущности (прямоугольник, скругление, цвет, обводка)
struct UiPaint {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    float radius = 0.0f;
    float borderW = 0.0f;
    float br = 0.45f;
    float bg = 0.50f;
    float bb = 0.62f;
    UiToken token = UiToken::Custom;
    uint8_t icon = 0;
    uint8_t plate = 0;
    uint8_t picked = 0;
    uint8_t hoverKids = 0;
    UiToken idleToken = UiToken::Custom;   // token while inactive, restored by uiSetActive(false)
    UiToken activeToken = UiToken::Custom; // token while active; Custom: the node has no active look
    UiRamp ramp = UiRamp::None;
    bool disabled = false;
    uint8_t iconLeft = 0;      // > 0: a labeled node draws its icon this many px from the left edge instead of the right
    uint8_t iconSide = 0;      // side of a left icon in px (0: 16)
    float shadow = 0.0f;
    float radiusTr = kUiUnset; // Если < 0.0f, наследует radius
    float radiusBr = kUiUnset;
    float radiusBl = kUiUnset;
    float hr = kUiUnset;       // Цвет при наведении (если < 0, управляется темой)
    float hg = kUiUnset;
    float hb = kUiUnset;
    float ha = kUiUnset;
    float angle = 0.0f;        // Поворот в градусах
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float bright = 1.0f;       // rgb *= bright
    float contrast = 1.0f;     // (rgb - 0.5) * contrast + 0.5
    float blur = 0.0f;         // Размытие текстуры

    [[nodiscard]] float effectiveRadiusTr() const noexcept { return radiusTr < 0.0f ? radius : radiusTr; }
    [[nodiscard]] float effectiveRadiusBr() const noexcept { return radiusBr < 0.0f ? radius : radiusBr; }
    [[nodiscard]] float effectiveRadiusBl() const noexcept { return radiusBl < 0.0f ? radius : radiusBl; }
    [[nodiscard]] bool hasHoverColor() const noexcept { return hr >= 0.0f && hg >= 0.0f && hb >= 0.0f; }
};

// Единая палитра кадра для смены тем оформления
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
    // Glyph ink of plate-less tool buttons by state (idle gray, hover white, active orange).
    float tool[3]{0.75f, 0.78f, 0.84f};
    float toolHover[3]{1.0f, 1.0f, 1.0f};
    float toolActive[3]{1.0f, 0.55f, 0.10f};
    float radius = 8.0f;
    float radiusSm = 6.0f;
    float motion = 0.14f;
    float hover = 0.08f;
    float titleBar = 40.0f;
    float resizeBorder = 6.0f;
    float advance = 9.0f;
    float glyphW = 8.0f;
    float glyphH = 18.0f;
    // Popup surface (window_panel class of assets/ui/styles.json); a negative radius means `radius`.
    float popupAlpha = 0.88f;
    float popupBorder = 1.0f;
    float popupRadius = -1.0f;
};

// A fill at or below this alpha is invisible: it neither blocks the pointer nor casts a shadow. A border thinner than
// kUiBorderMinW is not drawn.
constexpr float kUiInvisibleAlpha = 0.02f;
constexpr float kUiBorderMinW = 0.4f;

// Render-path numbers every node shares: the "ui" and "minimap" classes of assets/ui/styles.json (uiStyleLoad fills
// this). The member defaults are what the shipped sheet carries, so a state without a sheet draws the same.
struct UiPaintStyle {
    float disabledAlpha = 0.45f;  // alpha multiplier of a disabled node
    float hoverLift = 0.08f;      // channel lift of a hovered node when the look sets none
    float hoverMinAlpha = 0.12f;  // a hovered node fainter than this ...
    float hoverFallback = 0.28f;  // ... is drawn at this alpha
    float shadowDy = 0.15f;       // shadow offset down, in shadow radii
    float shadowAlpha = 0.42f;
    float shadowGrow = 1.0f;      // shadow spread, in shadow radii
    float iconSide = 14.0f;       // icon beside a label on the right, and of a node too small to fit one
    float iconLeft = 16.0f;       // icon on the left of a label unless the paint sets a side
    float iconInset = 6.0f;       // gap between a right-hand icon and the node edge
    float iconFitMin = 18.0f;     // a node smaller than this uses iconSide, a larger one fits (edge - iconFitPad)
    float iconFitPad = 4.0f;
    float iconMin = 12.0f;
    float iconMax = 72.0f;
    float toolIcon = 20.0f;       // glyph of a plate-less tool button
    float inkThreshold = 0.72f;   // luminance above which text on a fill switches to the dark ink
    float mapW = 72.0f;           // image minimap
    float mapH = 48.0f;
    float mapMargin = 8.0f;
    float mapPad = 2.0f;
    float mapAlpha = 0.9f;
    float mapRadius = 3.0f;
    float mapMinW = 120.0f;       // smallest image box that shows a minimap
    float mapMinH = 90.0f;
};

// Множитель альфы узла и его детей. 1 — без изменений.
struct UiFade {
    float a = 1.0f;
};

// Плавная цветовая анимация
struct UiAnim {
    float fr = 0.0f;
    float fg = 0.0f;
    float fb = 0.0f;
    float fa = 1.0f;
    float tr = 0.0f;
    float tg = 0.0f;
    float tb = 0.0f;
    float ta = 1.0f;
    float t0 = 0.0f;
    float dur = 0.0f;
    bool on = false;
};

static_assert(std::is_trivially_copyable_v<UiPaint>);
static_assert(std::is_trivially_copyable_v<UiLook>);
static_assert(std::is_trivially_copyable_v<UiAnim>);

inline void tokenRgb(const UiLook& look, UiToken token, float& r, float& g, float& b) noexcept {
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

inline float tokenRadius(const UiLook& look, UiToken token) noexcept {
    switch (token) {
    case UiToken::Button:
    case UiToken::Field:
    case UiToken::Menu:
    case UiToken::Check:
    case UiToken::Item:
    case UiToken::Close:
    case UiToken::Chrome:
        return look.radiusSm;
    case UiToken::Preview:
    case UiToken::Scroll:
        return look.radius;
    default:
        return -1.0f;
    }
}

inline UiPaint paintRgb(float r, float g, float b, float radius = 0.0f) noexcept {
    UiPaint out{};
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = 1.0f;
    out.radius = radius;
    return out;
}

inline UiPaint paintClear() noexcept {
    UiPaint out{};
    out.a = 0.0f;
    return out;
}

inline UiPaint paintToken(const UiLook& look, UiToken token, float radius = 0.0f) noexcept {
    UiPaint out = paintRgb(0.0f, 0.0f, 0.0f, radius);
    out.token = token;
    tokenRgb(look, token, out.r, out.g, out.b);
    return out;
}

inline UiPaint paintLabel(const UiLook& look) noexcept {
    UiPaint out = paintToken(look, UiToken::Text, 0.0f);
    out.a = 0.0f;
    out.borderW = 0.0f;
    return out;
}

inline void paintRaised(UiPaint& paint) noexcept {
    paint.borderW = 2.0f;
    paint.shadow = 7.0f;
    constexpr float lift = 0.34f;
    paint.br = paint.r + lift > 1.0f ? 1.0f : paint.r + lift;
    paint.bg = paint.g + lift > 1.0f ? 1.0f : paint.g + lift;
    paint.bb = paint.b + lift > 1.0f ? 1.0f : paint.b + lift;
}

// The one look of every popup surface (menus, choices, popovers): translucent window fill with a crisp 1px border.
inline UiPaint paintPopup(const UiLook& look) noexcept {
    UiPaint out = paintToken(look, UiToken::Window, look.popupRadius >= 0.0f ? look.popupRadius : look.radiusSm);
    out.a = look.popupAlpha;
    out.borderW = look.popupBorder;
    tokenRgb(look, UiToken::Muted, out.br, out.bg, out.bb);
    return out;
}

// X/Y/Z (and U/V) axis caps keep their conventional colors in every theme.
inline constexpr float kUiAxis[3][3]{
    {0.90f, 0.24f, 0.20f},
    {0.28f, 0.76f, 0.30f},
    {0.26f, 0.50f, 0.95f},
};

// Neutral tint constants for paints that carry no theme color (photo tint, ramp bases, marker rings).
inline constexpr float kUiWhite[3]{1.0f, 1.0f, 1.0f};
inline constexpr float kUiBlack[3]{0.0f, 0.0f, 0.0f};

inline void paintTint(UiPaint& paint, const float (&rgb)[3]) noexcept {
    paint.r = rgb[0];
    paint.g = rgb[1];
    paint.b = rgb[2];
}

inline void paintGhost(UiPaint& paint) noexcept {
    paint.token = UiToken::Custom;
    paint.a = 0.0f;
    paint.borderW = 0.0f;
    paint.shadow = 0.0f;
    paint.radius = 0.0f;
}

} // namespace burnhope
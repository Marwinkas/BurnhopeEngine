#pragma once

#include "ui/Geometry.hpp"
#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kUiFontGlyphs = 192;
constexpr uint32_t kUiGlyphStride = 16;
constexpr uint32_t kUiFontBytes = kUiFontGlyphs * kUiGlyphStride;
constexpr uint8_t kUiLineCap = 8;

enum class UiTextAlign : uint8_t { Left = 0, Center = 1, Right = 2, Justify = 3 };
enum class UiTextValign : uint8_t { Top = 0, Center = 1, Bottom = 2 };
enum class UiTextEllipsis : uint8_t { None = 0, End = 1, Middle = 2 };

enum class UiTextDeco : uint8_t {
    None          = 0,
    Underline     = 1 << 0,
    Strikethrough = 1 << 1,
    Overline      = 1 << 2,
};

inline constexpr UiTextDeco operator|(UiTextDeco a, UiTextDeco b) noexcept {
    return static_cast<UiTextDeco>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline constexpr bool operator&(UiTextDeco a, UiTextDeco b) noexcept {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

enum class UiTextTransform : uint8_t {
    None = 0,
    Uppercase = 1,
    Lowercase = 2,
    Capitalize = 3,
};

struct UiGlyphMetric {
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
    float bx = 0.0f;
    float by = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float advance = 0.0f;
};
inline uint32_t uiGlyphCodepoint(int index) noexcept {
    if (index >= 0 && index < 95) return static_cast<uint32_t>(32 + index);
    if (index == 95) return 0x0401; // 'Ё'
    if (index >= 96 && index < 160) return 0x0410u + static_cast<uint32_t>(index - 96); // 'А'-'я'
    if (index == 160) return 0x0451; // 'ё'
    return 0;
}
struct UiFontAtlas {
    UiGlyphMetric glyphs[kUiFontGlyphs]{};
    float fontPx = 32.0f;
    bool ready = false;
};

// Текстовый компонент сущности
struct UiText {
    char bytes[96]{};
    uint8_t len = 0;
    UiTextAlign align = UiTextAlign::Left;
    UiTextEllipsis ellipsis = UiTextEllipsis::None;
    UiTextValign valign = UiTextValign::Top;
    bool wrap = false;
    UiTextDeco deco = UiTextDeco::None;
    UiTextTransform transform = UiTextTransform::None;
    bool breakAll = false;
    uint8_t tabs = 0;
    float leading = 0.0f;
    float scroll = 0.0f;
    float tracking = 0.0f; // Межбуквенный интервал
    float wordGap = 0.0f;  // Межсловный интервал
    float indent = 0.0f;   // Красная строка
    uint8_t ownRun = 0;
    float size = 0.0f;     // Множитель масштаба (0 = 1.0)
    float thick = 0.0f;    // Толщина линии декора
    float under = 0.0f;    // Смещение подчеркивания
    float shadowX = 0.0f;
    float shadowY = 0.0f;
};

struct TextLine {
    uint8_t begin = 0;
    uint8_t len = 0;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float gap = 0.0f;
};

struct TextRun {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t lines = 0;
    UiTextDeco deco = UiTextDeco::None;
    float scale = 1.0f;
    float tracking = 0.0f;
    float wordGap = 0.0f;
    float thick = 1.0f;
    float under = 0.0f;
    float shadowX = 0.0f;
    float shadowY = 0.0f;
    TextLine line[kUiLineCap]{};
    UiBox box{};
};

inline int uiGlyphIndex(uint32_t cp) noexcept {
    if (cp >= 32 && cp <= 126) return static_cast<int>(cp - 32);
    if (cp == 0x0401) return 95; // 'Ё'
    if (cp >= 0x0410 && cp <= 0x044F) return 96 + static_cast<int>(cp - 0x0410); // 'А'-'я'
    if (cp == 0x0451) return 160; // 'ё'
    return -1;
}

inline uint32_t uiReadUtf8(const char* s, uint8_t len, uint8_t at, uint8_t& step) noexcept {
    step = 1;
    if (s == nullptr || at >= len) { step = 0; return 0; }
    const auto c0 = static_cast<unsigned char>(s[at]);
    if (c0 < 0x80) return c0;
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

static_assert(std::is_trivially_copyable_v<UiGlyphMetric>);
static_assert(std::is_trivially_copyable_v<UiFontAtlas>);
static_assert(std::is_trivially_copyable_v<UiText>);

} // namespace burnhope
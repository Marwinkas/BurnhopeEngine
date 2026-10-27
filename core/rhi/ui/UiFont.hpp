#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kUiFontGlyphs = 192;
constexpr uint32_t kUiGlyphStride = 16;
constexpr uint32_t kUiFontBytes = kUiFontGlyphs * kUiGlyphStride;

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

struct UiFontAtlas {
    UiGlyphMetric glyphs[kUiFontGlyphs]{};
    float fontPx = 32.0f;
    bool ready = false;
};

// Unicode codepoint of atlas slot `index` (ASCII 32..126, Ё, А-я, ё); 0 for an unused slot.
inline uint32_t uiGlyphCodepoint(int index) noexcept {
    if (index >= 0 && index < 95) return static_cast<uint32_t>(32 + index);
    if (index == 95) return 0x0401;
    if (index >= 96 && index < 160) return 0x0410u + static_cast<uint32_t>(index - 96);
    if (index == 160) return 0x0451;
    return 0;
}

static_assert(std::is_trivially_copyable_v<UiGlyphMetric>);
static_assert(std::is_trivially_copyable_v<UiFontAtlas>);

} // namespace burnhope

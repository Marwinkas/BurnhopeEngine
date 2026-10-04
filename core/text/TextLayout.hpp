#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr float kMsdfPxRange = 4.0f;

struct GlyphMetric {
    uint32_t codepoint = 0;
    float advanceX = 0.0f;
    float bearingX = 0.0f;
    float bearingY = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float uvMin[2]{};
    float uvMax[2]{};
    float reserved[2]{};
};

struct GlyphInstance {
    float posMin[2]{};
    float posMax[2]{};
    float uvMin[2]{};
    float uvMax[2]{};
    uint32_t color = 0xFFFFFFFFu;
    float pxRange = kMsdfPxRange;
    uint32_t pad[2]{};
};

// Address of the glyph slice goes through the existing meshDrawRecord push.
struct GlyphDrawPush {
    uint64_t glyphs = 0;
    uint32_t count = 0;
    uint32_t reserved = 0;
};

static_assert(std::is_trivially_copyable_v<GlyphMetric>);
static_assert(sizeof(GlyphMetric) == 48);
static_assert(std::is_trivially_copyable_v<GlyphInstance>);
static_assert(sizeof(GlyphInstance) == 48);
static_assert(sizeof(GlyphInstance) % 16 == 0);
static_assert(std::is_trivially_copyable_v<GlyphDrawPush>);
static_assert(sizeof(GlyphDrawPush) == 16);

inline const GlyphMetric* textFindGlyph(const GlyphMetric* atlas, uint32_t atlasCount, uint32_t codepoint) {
    if (atlas == nullptr) {
        return nullptr;
    }
    for (uint32_t i = 0; i < atlasCount; ++i) {
        if (atlas[i].codepoint == codepoint) {
            return &atlas[i];
        }
    }
    return nullptr;
}

inline uint32_t textDecodeUtf8(const char* utf8, uint32_t len, uint32_t& index) {
    if (utf8 == nullptr || index >= len) {
        return 0;
    }
    const auto lead = static_cast<uint8_t>(utf8[index]);
    if (lead < 0x80u) {
        index += 1;
        return lead;
    }
    uint32_t need = lead < 0xE0u ? 2u : (lead < 0xF0u ? 3u : 4u);
    if (index + need > len) {
        index += 1;
        return 0xFFFDu;
    }
    uint32_t code = lead & (need == 2u ? 0x1Fu : (need == 3u ? 0x0Fu : 0x07u));
    for (uint32_t n = 1; n < need; ++n) {
        const auto cont = static_cast<uint8_t>(utf8[index + n]);
        if ((cont & 0xC0u) != 0x80u) {
            index += 1;
            return 0xFFFDu;
        }
        code = (code << 6) | (cont & 0x3Fu);
    }
    index += need;
    return code;
}

inline void textPixelToClip(float x, float y, float viewW, float viewH, float& clipX, float& clipY) {
    const float width = viewW > 0.0f ? viewW : 1.0f;
    const float height = viewH > 0.0f ? viewH : 1.0f;
    clipX = x * (2.0f / width) - 1.0f;
    clipY = 1.0f - y * (2.0f / height);
}

inline uint32_t textLayoutLine(const char* utf8, uint32_t len, const GlyphMetric* atlas, uint32_t atlasCount, float startX, float startY, float viewW, float viewH, GlyphInstance* outGlyphs, uint32_t maxGlyphs) {
    if (outGlyphs == nullptr || maxGlyphs == 0) {
        return 0;
    }
    float pen = startX;
    uint32_t written = 0;
    uint32_t index = 0;
    while (index < len && written < maxGlyphs) {
        const uint32_t code = textDecodeUtf8(utf8, len, index);
        const GlyphMetric* metric = textFindGlyph(atlas, atlasCount, code);
        if (metric == nullptr) {
            continue;
        }
        GlyphInstance& glyph = outGlyphs[written];
        const float left = pen + metric->bearingX;
        const float top = startY - metric->bearingY;
        textPixelToClip(left, top, viewW, viewH, glyph.posMin[0], glyph.posMin[1]);
        textPixelToClip(left + metric->width, top + metric->height, viewW, viewH, glyph.posMax[0], glyph.posMax[1]);
        glyph.uvMin[0] = metric->uvMin[0];
        glyph.uvMin[1] = metric->uvMin[1];
        glyph.uvMax[0] = metric->uvMax[0];
        glyph.uvMax[1] = metric->uvMax[1];
        glyph.color = 0xFFFFFFFFu;
        glyph.pxRange = kMsdfPxRange;
        glyph.pad[0] = 0;
        glyph.pad[1] = 0;
        pen += metric->advanceX;
        written += 1;
    }
    return written;
}

} // namespace burnhope

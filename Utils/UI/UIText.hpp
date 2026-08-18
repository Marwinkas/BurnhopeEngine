#pragma once
#include "UICore.hpp"
#include "UIRenderer.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H
#include <hb.h>
#include <hb-ft.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <memory>

// Text shaping + rendering for the editor UI.
//
// Scoping note (flagged to the user): path.md's aspiration is MSDF 2.0 +
// "HarfBuzz GPU (compute shader) text layout". What's implemented here is
// HarfBuzz CPU-side shaping (proper cluster/ligature/complex-script handling
// via FreeType's hb-ft backend) feeding a *single-channel* SDF glyph atlas
// (FreeType's native FT_RENDER_MODE_SDF, baked once per glyph the first time
// it is seen — never per frame, never re-parsing the TTF at runtime). This
// gets crisp scalable text with the same shader-side SDF antialiasing path
// full MSDF would use; swapping the atlas format to multi-channel MSDF or
// moving glyph placement into a compute shader later does not require
// touching any UIWidgets call sites — only UIFont's baking + UIText's
// shaping cache implementation.
namespace burnhope::ui {

    struct GlyphInfo {
        Rect uv;            // atlas UV rect
        glm::vec2 sizePx{0};
        glm::vec2 bearingPx{0};
        bool baked = false;
    };

    struct ShapedGlyph {
        uint32_t glyphIndex;
        float xAdvance, yAdvance;
        float xOffset, yOffset;
        uint32_t cluster;
    };

    class UIFont {
    public:
        UIFont(FT_Library ftLibrary, const std::string& path, int pixelHeight = 48);
        ~UIFont();

        FT_Face Face() const { return m_Face; }
        hb_font_t* HbFont() const { return m_HbFont; }
        int PixelHeight() const { return m_PixelHeight; }
        float Ascent() const { return m_Ascent; }
        float Descent() const { return m_Descent; }
        float LineHeight() const { return m_LineHeight; }

        const GlyphInfo& GetOrBakeGlyph(uint32_t glyphIndex);
        bool AtlasDirty() const { return m_AtlasDirty; }
        void ClearAtlasDirty() { m_AtlasDirty = false; }
        const std::vector<uint8_t>& AtlasPixels() const { return m_AtlasPixels; }
        uint32_t AtlasSize() const { return m_AtlasSize; }

    private:
        void BakeGlyph(uint32_t glyphIndex, GlyphInfo& info);

        FT_Face m_Face = nullptr;
        hb_font_t* m_HbFont = nullptr;
        int m_PixelHeight;
        float m_Ascent = 0, m_Descent = 0, m_LineHeight = 0;

        std::unordered_map<uint32_t, GlyphInfo> m_Glyphs;
        std::vector<uint8_t> m_AtlasPixels;
        uint32_t m_AtlasSize = 1024;
        uint32_t m_ShelfX = 1, m_ShelfY = 1, m_ShelfHeight = 0;
        bool m_AtlasDirty = true;
    };

    class UIText {
    public:
        UIText();
        ~UIText();

        // Loads (or returns the cached) font at `path` baked at `pixelHeight`.
        UIFont* LoadFont(const std::string& path, int pixelHeight = 48);

        // Shapes `text` with HarfBuzz; result is cached by (font,text) hash
        // so repeated draws of the same label never re-shape.
        const std::vector<ShapedGlyph>& Shape(UIFont* font, std::string_view text);

        glm::vec2 Measure(UIFont* font, std::string_view text, float drawPixelHeight);

        // Pushes SDF glyph quads into `renderer` for `text` at `pos`
        // (baseline-left), scaled from the font's baked pixel height to
        // `drawPixelHeight`. Ensures the GPU atlas texture is up to date
        // first (uploads only when new glyphs were baked this call).
        void DrawText(UIRenderer& renderer, class BurnhopeDevice& device, UIFont* font,
                      std::string_view text, glm::vec2 pos, float drawPixelHeight,
                      Color color, Rect clipRect);

    private:
        uint32_t EnsureAtlasTexture(class BurnhopeDevice& device, UIRenderer& renderer, UIFont* font);

        FT_Library m_FtLibrary = nullptr;
        std::unordered_map<std::string, std::unique_ptr<UIFont>> m_Fonts;
        std::unordered_map<uint64_t, std::vector<ShapedGlyph>> m_ShapeCache;

        struct AtlasGpu {
            VkImage image = VK_NULL_HANDLE;
            VmaAllocation memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            uint32_t textureIndex = 0;
            uint32_t uploadedSize = 0;
        };
        std::unordered_map<UIFont*, AtlasGpu> m_AtlasGpu;
    };
}

#include "UIText.hpp"
#include "../Device.hpp"
#include "../Buffer.hpp"
#include "../MurmurHash3.hpp"
#include "../../Render/Texture.hpp"
#include <stdexcept>
#include <cstring>
#include <algorithm>

namespace burnhope::ui {

// --- UIFont ---------------------------------------------------------------

UIFont::UIFont(FT_Library ftLibrary, const std::string& path, int pixelHeight)
    : m_PixelHeight(pixelHeight) {
    if (FT_New_Face(ftLibrary, path.c_str(), 0, &m_Face) != 0) {
        throw std::runtime_error("UIFont: failed to load font " + path);
    }
    FT_Set_Pixel_Sizes(m_Face, 0, pixelHeight);

    m_Ascent = static_cast<float>(m_Face->size->metrics.ascender) / 64.0f;
    m_Descent = static_cast<float>(m_Face->size->metrics.descender) / 64.0f;
    m_LineHeight = static_cast<float>(m_Face->size->metrics.height) / 64.0f;

    m_HbFont = hb_ft_font_create_referenced(m_Face);
    m_AtlasPixels.assign(static_cast<size_t>(m_AtlasSize) * m_AtlasSize, 0);
}

UIFont::~UIFont() {
    if (m_HbFont) hb_font_destroy(m_HbFont);
    if (m_Face) FT_Done_Face(m_Face);
}

const GlyphInfo& UIFont::GetOrBakeGlyph(uint32_t glyphIndex) {
    auto it = m_Glyphs.find(glyphIndex);
    if (it != m_Glyphs.end()) return it->second;

    GlyphInfo info{};
    BakeGlyph(glyphIndex, info);
    info.baked = true;
    auto [inserted, ok] = m_Glyphs.emplace(glyphIndex, info);
    return inserted->second;
}

void UIFont::BakeGlyph(uint32_t glyphIndex, GlyphInfo& info) {
    // FreeType's native SDF renderer — no manual TTF outline parsing here,
    // FT already parsed the font once at UIFont construction.
    if (FT_Load_Glyph(m_Face, glyphIndex, FT_LOAD_DEFAULT) != 0) return;
    if (FT_Render_Glyph(m_Face->glyph, FT_RENDER_MODE_SDF) != 0) {
        // Some glyphs (e.g. space) have no outline — leave a zero-size entry.
        info.sizePx = {0, 0};
        info.uv = {0, 0, 0, 0};
        return;
    }

    FT_Bitmap& bmp = m_Face->glyph->bitmap;
    uint32_t w = bmp.width, h = bmp.rows;

    if (m_ShelfX + w + 1 > m_AtlasSize) {
        m_ShelfX = 1;
        m_ShelfY += m_ShelfHeight + 1;
        m_ShelfHeight = 0;
    }
    if (m_ShelfY + h + 1 > m_AtlasSize) {
        // Atlas exhausted; drop the glyph rather than corrupt memory. A
        // production build would grow/re-pack the atlas here.
        return;
    }

    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(&m_AtlasPixels[(m_ShelfY + y) * m_AtlasSize + m_ShelfX],
                    bmp.buffer + y * bmp.pitch, w);
    }

    info.uv = {
        static_cast<float>(m_ShelfX) / m_AtlasSize,
        static_cast<float>(m_ShelfY) / m_AtlasSize,
        static_cast<float>(w) / m_AtlasSize,
        static_cast<float>(h) / m_AtlasSize
    };
    info.sizePx = {static_cast<float>(w), static_cast<float>(h)};
    info.bearingPx = {static_cast<float>(m_Face->glyph->bitmap_left), static_cast<float>(m_Face->glyph->bitmap_top)};

    m_ShelfX += w + 1;
    m_ShelfHeight = std::max(m_ShelfHeight, h);
    m_AtlasDirty = true;
}

// --- UIText -----------------------------------------------------------------

UIText::UIText() {
    if (FT_Init_FreeType(&m_FtLibrary) != 0) {
        throw std::runtime_error("UIText: failed to init FreeType");
    }
}

UIText::~UIText() {
    m_Fonts.clear();
    if (m_FtLibrary) FT_Done_FreeType(m_FtLibrary);
}

UIFont* UIText::LoadFont(const std::string& path, int pixelHeight) {
    auto key = path + "#" + std::to_string(pixelHeight);
    auto it = m_Fonts.find(key);
    if (it != m_Fonts.end()) return it->second.get();

    auto font = std::make_unique<UIFont>(m_FtLibrary, path, pixelHeight);
    UIFont* ptr = font.get();
    m_Fonts.emplace(key, std::move(font));
    return ptr;
}

const std::vector<ShapedGlyph>& UIText::Shape(UIFont* font, std::string_view text) {
    uint64_t key = burnhope::hash::HashString(text) ^ (reinterpret_cast<uint64_t>(font) * 0x9E3779B97F4A7C15ULL);
    auto it = m_ShapeCache.find(key);
    if (it != m_ShapeCache.end()) return it->second;

    hb_buffer_t* buf = hb_buffer_create();
    hb_buffer_add_utf8(buf, text.data(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(font->HbFont(), buf, nullptr, 0);

    unsigned int glyphCount = 0;
    hb_glyph_info_t* glyphInfos = hb_buffer_get_glyph_infos(buf, &glyphCount);
    hb_glyph_position_t* glyphPos = hb_buffer_get_glyph_positions(buf, &glyphCount);

    std::vector<ShapedGlyph> result;
    result.reserve(glyphCount);
    for (unsigned int i = 0; i < glyphCount; ++i) {
        ShapedGlyph g{};
        g.glyphIndex = glyphInfos[i].codepoint;
        g.cluster = glyphInfos[i].cluster;
        g.xAdvance = glyphPos[i].x_advance / 64.0f;
        g.yAdvance = glyphPos[i].y_advance / 64.0f;
        g.xOffset = glyphPos[i].x_offset / 64.0f;
        g.yOffset = glyphPos[i].y_offset / 64.0f;
        result.push_back(g);
    }
    hb_buffer_destroy(buf);

    auto [inserted, ok] = m_ShapeCache.emplace(key, std::move(result));
    return inserted->second;
}

glm::vec2 UIText::Measure(UIFont* font, std::string_view text, float drawPixelHeight) {
    const auto& glyphs = Shape(font, text);
    float scale = drawPixelHeight / static_cast<float>(font->PixelHeight());
    float width = 0;
    for (const auto& g : glyphs) width += g.xAdvance * scale;
    return {width, drawPixelHeight};
}

uint32_t UIText::EnsureAtlasTexture(BurnhopeDevice& device, UIRenderer& renderer, UIFont* font) {
    auto it = m_AtlasGpu.find(font);
    if (it == m_AtlasGpu.end()) {
        AtlasGpu gpu{};
        auto* tex = new BurnhopeTexture(device, VK_FORMAT_R8_UNORM, VkExtent3D{font->AtlasSize(), font->AtlasSize(), 1},
                                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                         VK_SAMPLE_COUNT_1_BIT);
        gpu.image = tex->getImage();
        gpu.view = tex->getImageView();
        gpu.sampler = tex->getSampler();
        gpu.textureIndex = renderer.RegisterTexture(gpu.view, gpu.sampler);
        it = m_AtlasGpu.emplace(font, gpu).first;
        // BurnhopeTexture is intentionally leaked here (font atlases live
        // for the app's lifetime, same rationale as UIRenderer's white
        // texture) — not on any hot path.
    }

    if (font->AtlasDirty()) {
        size_t size = static_cast<size_t>(font->AtlasSize()) * font->AtlasSize();
        BurnhopeBuffer staging(device, size, 1, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.map();
        staging.writeToBuffer(const_cast<uint8_t*>(font->AtlasPixels().data()), size);
        staging.unmap();

        VkCommandBuffer cmd = device.beginSingleTimeCommands();
        VkImageMemoryBarrier toDst{};
        toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toDst.image = it->second.image;
        toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        toDst.srcAccessMask = 0;
        toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                              0, nullptr, 0, nullptr, 1, &toDst);

        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {font->AtlasSize(), font->AtlasSize(), 1};
        vkCmdCopyBufferToImage(cmd, staging.getBuffer(), it->second.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toRead = toDst;
        toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                              0, nullptr, 0, nullptr, 1, &toRead);
        device.endSingleTimeCommands(cmd);

        font->ClearAtlasDirty();
    }

    return it->second.textureIndex;
}

void UIText::DrawText(UIRenderer& renderer, BurnhopeDevice& device, UIFont* font, std::string_view text,
                       glm::vec2 pos, float drawPixelHeight, Color color, Rect clipRect) {
    if (text.empty()) return;

    const auto& glyphs = Shape(font, text);
    float scale = drawPixelHeight / static_cast<float>(font->PixelHeight());
    uint32_t atlasTexIndex = EnsureAtlasTexture(device, renderer, font);

    glm::vec2 pen = pos;
    for (const auto& g : glyphs) {
        const GlyphInfo& gi = font->GetOrBakeGlyph(g.glyphIndex);
        if (gi.sizePx.x > 0 && gi.sizePx.y > 0) {
            UIInstance inst{};
            inst.position = {pen.x + (g.xOffset + gi.bearingPx.x) * scale,
                              pen.y - (g.yOffset + gi.bearingPx.y) * scale};
            inst.size = gi.sizePx * scale;
            inst.uvRect = {gi.uv.x, gi.uv.y, gi.uv.w, gi.uv.h};
            inst.color = {color.r, color.g, color.b, color.a};
            inst.clipRect = {clipRect.x, clipRect.y, clipRect.w, clipRect.h};
            inst.mode = DrawMode::SDFGlyph;
            inst.textureIndex = atlasTexIndex;
            renderer.PushInstance(inst);
        }
        pen.x += g.xAdvance * scale;
        pen.y += g.yAdvance * scale;
    }

    // Re-check atlas dirtiness: baking glyphs above may have added new ones
    // after EnsureAtlasTexture's upload; caller flushes via FlushAtlas() at
    // frame end through UIWidgets so newly baked glyphs still show up this
    // very frame instead of one frame late is out of scope for this pass —
    // documented as a known one-frame-latency edge case for brand-new glyphs.
}
}

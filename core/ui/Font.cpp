#include "ui/Font.hpp"
#include "ui/Model.hpp"
#include "image/ImageFile.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MODULE_H

#include <spdlog/spdlog.h>

#include <cstring>
#include <vector>

namespace burnhope {
namespace {

constexpr int kCell = 64;
constexpr int kCols = 16;
constexpr int kRows = 12;

uint32_t codepointAt(int index) {
    if (index < 95) {
        return static_cast<uint32_t>(32 + index);
    }
    if (index == 95) {
        return 0x0401;
    }
    if (index < 160) {
        return 0x0410u + static_cast<uint32_t>(index - 96);
    }
    if (index == 160) {
        return 0x0451;
    }
    return 0;
}

bool uploadAtlas(Device& device, DescriptorHeaps& heaps, GpuImage& atlas, const std::vector<uint8_t>& pixels, int width, int height) {
    if (!gpuImageUploadR8(atlas, device, pixels.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        return false;
    }
    if (!heapWriteImage(
            heaps,
            device,
            kUiDescFont,
            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
            atlas,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_ASPECT_COLOR_BIT)) {
        return false;
    }
    VkSamplerCreateInfo sampler{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 16.0f,
    };
    return heapWriteSampler(heaps, device, 0, sampler);
}

} // namespace

bool fontLoadSdf(
    UiState& ui,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    const char* path,
    float pixelSize) {
    if (path == nullptr || pixelSize < 4.0f) {
        return false;
    }
    FT_Library library{};
    if (FT_Init_FreeType(&library) != 0) {
        spdlog::error("freetype init failed");
        return false;
    }
    FT_UInt spread = 2;
    FT_Property_Set(library, "sdf", "spread", &spread);
    FT_Face face{};
    if (FT_New_Face(library, path, 0, &face) != 0) {
        FT_Done_FreeType(library);
        return false;
    }
    FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));

    const int width = kCols * kCell;
    const int height = kRows * kCell;
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height), 0);

    for (int i = 0; i < static_cast<int>(kUiFontGlyphs); ++i) {
        const uint32_t cp = codepointAt(i);
        ui.font.glyphs[i].advance = pixelSize * 0.5f;
        ui.font.glyphs[i].w = 0;
        ui.font.glyphs[i].h = 0;
        if (cp == 0 || FT_Load_Char(face, cp, FT_LOAD_DEFAULT) != 0) {
            continue;
        }
        if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_SDF) != 0) {
            continue;
        }
        const FT_Bitmap& bmp = face->glyph->bitmap;
        const int col = i % kCols;
        const int row = i / kCols;
        const int ox = col * kCell + (kCell - static_cast<int>(bmp.width)) / 2;
        const int oy = row * kCell + (kCell - static_cast<int>(bmp.rows)) / 2;
        for (unsigned y = 0; y < bmp.rows; ++y) {
            const int dy = oy + static_cast<int>(y);
            if (dy < 0 || dy >= height) {
                continue;
            }
            for (unsigned x = 0; x < bmp.width; ++x) {
                const int dx = ox + static_cast<int>(x);
                if (dx < 0 || dx >= width) {
                    continue;
                }
                const unsigned char* src = bmp.buffer + static_cast<int>(y) * bmp.pitch;
            pixels[static_cast<size_t>(dy * width + dx)] = src[x];
            }
        }
        ui.font.glyphs[i].u0 = static_cast<float>(ox) / static_cast<float>(width);
        ui.font.glyphs[i].v0 = static_cast<float>(oy) / static_cast<float>(height);
        ui.font.glyphs[i].u1 = static_cast<float>(ox + static_cast<int>(bmp.width)) / static_cast<float>(width);
        ui.font.glyphs[i].v1 = static_cast<float>(oy + static_cast<int>(bmp.rows)) / static_cast<float>(height);
        ui.font.glyphs[i].bx = static_cast<float>(face->glyph->bitmap_left);
        ui.font.glyphs[i].by = static_cast<float>(face->glyph->bitmap_top);
        ui.font.glyphs[i].w = static_cast<float>(bmp.width);
        ui.font.glyphs[i].h = static_cast<float>(bmp.rows);
        ui.font.glyphs[i].advance = static_cast<float>(face->glyph->advance.x) / 64.0f;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);

    if (!uploadAtlas(device, heaps, atlas, pixels, width, height)) {
        spdlog::error("sdf atlas upload failed");
        return false;
    }
    ui.font.fontPx = pixelSize;
    ui.font.ready = true;
    ui.look.advance = ui.font.glyphs[static_cast<int>('M' - 32)].advance * (ui.look.glyphH / pixelSize);
    if (ui.look.advance < 4.0f) {
        ui.look.advance = 9.0f;
    }
    spdlog::info("sdf font {} at {} px", path, pixelSize);
    return true;
}

} // namespace burnhope

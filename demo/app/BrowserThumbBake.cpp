#include "app/BrowserThumbsInternal.hpp"

#include "asset/MaterialBake.hpp"
#include "image/ImageDecode.hpp"
#include "gui/GuiIcons.hpp"
#include "platform/PlatformUtil.hpp"

#include <algorithm>
#include <cstring>

namespace burnhope {
namespace {

constexpr uint8_t kKindColor[kFileKinds][3] = {
    {255, 184, 76}, {110, 170, 255}, {90, 210, 180}, {140, 200, 110}, {210, 120, 220}, {170, 175, 185},
};
constexpr const char* kKindIcon[kFileKinds] = {"folder", "scene", "file_box", "file_image", "file_braces", "file"};

uint8_t* cellPixels(ThumbCache& c, uint32_t cell) {
    const size_t x = (cell % kThumbGrid) * kThumbPx;
    const size_t y = (cell / kThumbGrid) * kThumbPx;
    return c.atlas.data() + (y * kThumbGrid * kThumbPx + x) * 4u;
}

// Area-averages an RGBA image into the 64 px cell, keeping the aspect ratio and centring it on the plate.
void blitScaled(ThumbCache& c, uint32_t cell, const uint8_t* src, uint32_t sw, uint32_t sh) {
    thumbFillPlate(c, cell);
    if (sw == 0 || sh == 0) {
        return;
    }
    const float scale = std::min(static_cast<float>(kThumbPx) / static_cast<float>(sw), static_cast<float>(kThumbPx) / static_cast<float>(sh));
    const uint32_t dw = std::max(1u, static_cast<uint32_t>(static_cast<float>(sw) * scale));
    const uint32_t dh = std::max(1u, static_cast<uint32_t>(static_cast<float>(sh) * scale));
    const uint32_t ox = (kThumbPx - dw) / 2;
    const uint32_t oy = (kThumbPx - dh) / 2;
    uint8_t* base = cellPixels(c, cell);
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t y0 = y * sh / dh;
        const uint32_t y1 = std::max(y0 + 1, (y + 1) * sh / dh);
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t x0 = x * sw / dw;
            const uint32_t x1 = std::max(x0 + 1, (x + 1) * sw / dw);
            uint32_t sum[4] = {};
            uint32_t n = 0;
            for (uint32_t sy = y0; sy < std::min(y1, sh); ++sy) {
                for (uint32_t sx = x0; sx < std::min(x1, sw); ++sx) {
                    const uint8_t* p = src + (static_cast<size_t>(sy) * sw + sx) * 4u;
                    sum[0] += p[0] * p[3];
                    sum[1] += p[1] * p[3];
                    sum[2] += p[2] * p[3];
                    sum[3] += p[3];
                    ++n;
                }
            }
            uint8_t* dst = base + (static_cast<size_t>(oy + y) * kThumbGrid * kThumbPx + ox + x) * 4u;
            const uint32_t alpha = n != 0 ? sum[3] / n : 0;
            for (int ch = 0; ch < 3; ++ch) {
                const uint32_t color = sum[3] != 0 ? sum[ch] / sum[3] : 0;
                dst[ch] = static_cast<uint8_t>((color * alpha + c.plate[ch] * (255 - alpha)) / 255);
            }
            dst[3] = 255;
        }
    }
}

} // namespace

uint64_t thumbHashPath(const char* path) {
    uint64_t h = 1469598103934665603ull;
    for (; *path != '\0'; ++path) {
        h = (h ^ static_cast<uint8_t>(*path)) * 1099511628211ull;
    }
    return h != 0 ? h : 1;
}

void thumbFillPlate(ThumbCache& c, uint32_t cell) {
    uint8_t* base = cellPixels(c, cell);
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        uint8_t* row = base + static_cast<size_t>(y) * kThumbGrid * kThumbPx * 4u;
        for (uint32_t x = 0; x < kThumbPx; ++x) {
            row[x * 4 + 0] = c.plate[0];
            row[x * 4 + 1] = c.plate[1];
            row[x * 4 + 2] = c.plate[2];
            row[x * 4 + 3] = 255;
        }
    }
}

bool thumbBakeKindIcon(ThumbCache& c, uint32_t cell, FileKind kind) {
    const uint32_t k = static_cast<uint32_t>(kind);
    thumbFillPlate(c, cell);
    char resolved[640]{};
    const char* asset = gui::guiIconPath(gui::guiIconFind(kKindIcon[k]));
    uint8_t mask[kThumbPx * kThumbPx * 4]{};
    if (asset == nullptr || !platformResolveAsset(asset, resolved, sizeof(resolved)) ||
        !imageRasterSvgCell(resolved, mask, kThumbPx, 0, 0, kThumbPx)) {
        return false;
    }
    uint8_t* base = cellPixels(c, cell);
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        for (uint32_t x = 0; x < kThumbPx; ++x) {
            const uint32_t a = mask[(y * kThumbPx + x) * 4 + 3];
            uint8_t* dst = base + (static_cast<size_t>(y) * kThumbGrid * kThumbPx + x) * 4u;
            for (int ch = 0; ch < 3; ++ch) {
                dst[ch] = static_cast<uint8_t>((kKindColor[k][ch] * a + dst[ch] * (255 - a)) / 255);
            }
        }
    }
    return true;
}

bool thumbBakeTexture(ThumbCache& c, uint32_t cell, const char* path) {
    ImageCpu img{};
    if (!imageLoadFile(img, path) || img.rgba == nullptr) {
        imageCpuFree(img);
        return false;
    }
    blitScaled(c, cell, img.rgba, img.width, img.height);
    imageCpuFree(img);
    return true;
}

bool thumbBakeMaterial(ThumbCache& c, uint32_t cell, const char* path) {
    BhmatDoc doc{};
    if (!materialLoadBhmat(path, doc)) {
        return false;
    }
    materialBakeAtlas(doc, path, c.matAtlas.data());
    materialBakePreview(doc, c.matAtlas.data(), MatShape::Sphere, 0.6f, -0.25f, 1.0f, c.matPreview.data());
    blitScaled(c, cell, c.matPreview.data(), kMatSphereSide, kMatSphereSide);
    return true;
}

void thumbCopyCell(ThumbCache& c, uint32_t cell, const uint8_t* rgba) {
    uint8_t* base = cellPixels(c, cell);
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        std::memcpy(base + static_cast<size_t>(y) * kThumbGrid * kThumbPx * 4u, rgba + static_cast<size_t>(y) * kThumbPx * 4u, kThumbPx * 4u);
    }
}

} // namespace burnhope

#include "app/MatThumb.hpp"
#include "app/ModelThumb.hpp"

#include "Str.hpp"
#include "asset/MaterialBake.hpp"
#include "image/ImageDecode.hpp"
#include "files/Browser.hpp"
#include "files/Format.hpp"
#include "ui/rhi/Canvas.hpp"

#include <algorithm>

namespace burnhope {

namespace {

constexpr uint32_t kAtlasSide = kThumbCols * kThumbPx;

// Box-filters the kMatSphereSide^2 preview down into cell `cell` of the atlas.
void blit(MatThumbs& t, uint8_t cell) {
    const uint32_t ox = (cell % kThumbCols) * kThumbPx;
    const uint32_t oy = (cell / kThumbCols) * kThumbPx;
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        const uint32_t y0 = y * kMatSphereSide / kThumbPx;
        const uint32_t y1 = std::max(y0 + 1, (y + 1) * kMatSphereSide / kThumbPx);
        for (uint32_t x = 0; x < kThumbPx; ++x) {
            const uint32_t x0 = x * kMatSphereSide / kThumbPx;
            const uint32_t x1 = std::max(x0 + 1, (x + 1) * kMatSphereSide / kThumbPx);
            uint32_t sum[4]{};
            for (uint32_t sy = y0; sy < y1; ++sy) {
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* src = t.shaded.data() + (static_cast<size_t>(sy) * kMatSphereSide + sx) * 4u;
                    for (uint32_t c = 0; c < 4; ++c) {
                        sum[c] += src[c];
                    }
                }
            }
            const uint32_t n = (y1 - y0) * (x1 - x0);
            uint8_t* dst = t.atlas.data() + (static_cast<size_t>(oy + y) * kAtlasSide + ox + x) * 4u;
            for (uint32_t c = 0; c < 4; ++c) {
                dst[c] = static_cast<uint8_t>(sum[c] / n);
            }
        }
    }
}

// Fits the first frame of the image at `path` into cell `cell` (aspect kept, transparent margin, alpha-weighted box filter).
bool bakeImage(MatThumbs& t, const char* path, uint8_t cell) {
    ImageCpu img{};
    if (!imageLoadFile(img, path) || img.rgba == nullptr || img.width == 0 || img.height == 0 || img.frames == 0) {
        imageCpuFree(img);
        return false;
    }
    const uint32_t sw = img.width;
    const uint32_t sh = img.height / img.frames;
    const bool wide = static_cast<uint64_t>(sw) >= sh;
    const uint32_t dw = wide ? kThumbPx : std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(sw) * kThumbPx / sh));
    const uint32_t dh = wide ? std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(sh) * kThumbPx / sw)) : kThumbPx;
    const uint32_t ox = (cell % kThumbCols) * kThumbPx;
    const uint32_t oy = (cell / kThumbCols) * kThumbPx;
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        uint8_t* row = t.atlas.data() + (static_cast<size_t>(oy + y) * kAtlasSide + ox) * 4u;
        std::fill(row, row + kThumbPx * 4u, uint8_t{0});
    }
    const uint32_t padX = (kThumbPx - dw) / 2;
    const uint32_t padY = (kThumbPx - dh) / 2;
    for (uint32_t y = 0; y < dh; ++y) {
        const uint32_t y0 = static_cast<uint32_t>(static_cast<uint64_t>(y) * sh / dh);
        const uint32_t y1 = std::max(y0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(y + 1) * sh / dh));
        for (uint32_t x = 0; x < dw; ++x) {
            const uint32_t x0 = static_cast<uint32_t>(static_cast<uint64_t>(x) * sw / dw);
            const uint32_t x1 = std::max(x0 + 1, static_cast<uint32_t>(static_cast<uint64_t>(x + 1) * sw / dw));
            uint64_t rgb[3]{};
            uint64_t alpha = 0;
            for (uint32_t sy = y0; sy < y1; ++sy) {
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* src = img.rgba + (static_cast<size_t>(sy) * sw + sx) * 4u;
                    for (uint32_t c = 0; c < 3; ++c) {
                        rgb[c] += static_cast<uint64_t>(src[c]) * src[3];
                    }
                    alpha += src[3];
                }
            }
            const uint32_t n = (y1 - y0) * (x1 - x0);
            uint8_t* dst = t.atlas.data() + (static_cast<size_t>(oy + padY + y) * kAtlasSide + ox + padX + x) * 4u;
            for (uint32_t c = 0; c < 3; ++c) {
                dst[c] = alpha != 0 ? static_cast<uint8_t>(rgb[c] / alpha) : uint8_t{0};
            }
            dst[3] = static_cast<uint8_t>(alpha / n);
        }
    }
    imageCpuFree(img);
    t.dirty = 1;
    return true;
}

bool bake(void* user, const char* path, uint8_t cell) {
    auto* t = static_cast<MatThumbs*>(user);
    if (t != nullptr && cell < kThumbCells && fileIsModel(path)) {
        const bool ok = modelThumbBake(t->atlas.data(), cell, path);
        t->dirty = ok ? 1 : t->dirty;
        return ok;
    }
    if (t != nullptr && cell < kThumbCells && !strExtIs(path, ".bhmat")) {
        return bakeImage(*t, path, cell);
    }
    BhmatDoc doc{};
    if (t == nullptr || cell >= kThumbCells || !materialLoadBhmat(path, doc)) {
        return false;
    }
    materialBakeAtlas(doc, path, t->maps.data());
    materialBakePreview(doc, t->maps.data(), MatShape::Sphere, 0.0f, 0.0f, 1.0f, t->shaded.data());
    blit(*t, cell);
    t->dirty = 1;
    return true;
}

} // namespace

void matThumbsAttach(MatThumbs& thumbs, Canvas& canvas) {
    thumbs.canvas = &canvas;
    thumbs.atlas.assign(static_cast<size_t>(kAtlasSide) * kAtlasSide * 4u, 0);
    thumbs.maps.resize(static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u);
    thumbs.shaded.resize(static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u);
    thumbs.dirty = 1;
    if (canvas.state != nullptr) {
        filesThumbs(*canvas.state, kMatThumbSlot, bake, &thumbs);
    }
}

void matThumbsFlush(MatThumbs& thumbs) {
    if (thumbs.dirty == 0 || thumbs.canvas == nullptr || thumbs.canvas->device == nullptr) {
        return;
    }
    if (canvasUploadPhoto(*thumbs.canvas, kMatThumbSlot, thumbs.atlas.data(), kAtlasSide, kAtlasSide)) {
        thumbs.dirty = 0;
    }
}

} // namespace burnhope

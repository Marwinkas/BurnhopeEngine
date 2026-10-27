#include "app/BrowserThumbs.hpp"

#include "app/BrowserController.hpp"
#include "app/Engine.hpp"
#include "asset/MaterialBake.hpp"
#include "image/ImageDecode.hpp"
#include "gui/data/GuiIcons.hpp"
#include "platform/PlatformUtil.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace burnhope {
namespace {

constexpr uint32_t kBakePerFrame = 3;
constexpr uint8_t kPlate[3] = {38, 40, 48};
constexpr uint8_t kKindColor[kFileKinds][3] = {
    {255, 184, 76}, {110, 170, 255}, {90, 210, 180}, {140, 200, 110}, {210, 120, 220}, {170, 175, 185},
};
constexpr const char* kKindIcon[kFileKinds] = {"folder", "scene", "file_box", "file_image", "file_braces", "file"};

uint64_t hashPath(const char* path) {
    uint64_t h = 1469598103934665603ull;
    for (; *path != '\0'; ++path) {
        h = (h ^ static_cast<uint8_t>(*path)) * 1099511628211ull;
    }
    return h != 0 ? h : 1;
}

uint8_t* cellPixels(ThumbCache& c, uint32_t cell) {
    const size_t x = (cell % kThumbGrid) * kThumbPx;
    const size_t y = (cell / kThumbGrid) * kThumbPx;
    return c.atlas.get() + (y * kThumbGrid * kThumbPx + x) * 4u;
}

void fillPlate(ThumbCache& c, uint32_t cell) {
    uint8_t* base = cellPixels(c, cell);
    for (uint32_t y = 0; y < kThumbPx; ++y) {
        uint8_t* row = base + static_cast<size_t>(y) * kThumbGrid * kThumbPx * 4u;
        for (uint32_t x = 0; x < kThumbPx; ++x) {
            row[x * 4 + 0] = kPlate[0];
            row[x * 4 + 1] = kPlate[1];
            row[x * 4 + 2] = kPlate[2];
            row[x * 4 + 3] = 255;
        }
    }
}

// Area-averages an RGBA image into the 64 px cell, keeping the aspect ratio and centring it on the plate.
void blitScaled(ThumbCache& c, uint32_t cell, const uint8_t* src, uint32_t sw, uint32_t sh) {
    fillPlate(c, cell);
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
                dst[ch] = static_cast<uint8_t>((color * alpha + kPlate[ch] * (255 - alpha)) / 255);
            }
            dst[3] = 255;
        }
    }
}

bool bakeKindIcon(ThumbCache& c, uint32_t cell, FileKind kind) {
    const uint32_t k = static_cast<uint32_t>(kind);
    fillPlate(c, cell);
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

bool bakeTexture(ThumbCache& c, uint32_t cell, const char* path) {
    ImageCpu img{};
    if (!imageLoadFile(img, path) || img.rgba == nullptr) {
        imageCpuFree(img);
        return false;
    }
    blitScaled(c, cell, img.rgba, img.width, img.height);
    imageCpuFree(img);
    return true;
}

bool bakeMaterial(ThumbCache& c, uint32_t cell, const char* path) {
    BhmatDoc doc{};
    if (!materialLoadBhmat(path, doc)) {
        return false;
    }
    materialBakeAtlas(doc, path, c.matAtlas.get());
    materialBakePreview(doc, c.matAtlas.get(), MatShape::Sphere, 0.6f, -0.25f, 1.0f, c.matPreview.get());
    blitScaled(c, cell, c.matPreview.get(), kMatSphereSide, kMatSphereSide);
    return true;
}

// Least recently used preview cell (kind cells are never evicted).
uint32_t pickCell(const ThumbCache& c) {
    uint32_t best = kFileKinds;
    for (uint32_t i = kFileKinds; i < kThumbCells; ++i) {
        if (c.key[i] == 0) {
            return i;
        }
        if (c.used[i] < c.used[best]) {
            best = i;
        }
    }
    return best;
}

void setThumb(Engine& e, uint32_t index, uint8_t cell) {
    e.browser.files[index].cell = cell;
    guiHostSetListThumb(e.gui, browserGridPath(), index, cell);
}

} // namespace

bool thumbsInit(ThumbCache& c) {
    c.atlas.reset(new (std::nothrow) uint8_t[static_cast<size_t>(kThumbGrid) * kThumbPx * kThumbGrid * kThumbPx * 4u]());
    c.matAtlas.reset(new (std::nothrow) uint8_t[static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u]());
    c.matPreview.reset(new (std::nothrow) uint8_t[static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u]());
    return c.atlas != nullptr && c.matAtlas != nullptr && c.matPreview != nullptr;
}

void thumbsSync(Engine& e) {
    BrowserState& b = e.browser;
    ThumbCache& c = b.thumbs;
    Canvas* canvas = hostCanvas(e.host, e.slot);
    if (canvas == nullptr || c.atlas == nullptr) {
        return;
    }
    if (!c.kindsReady) {
        for (uint32_t k = 0; k < kFileKinds; ++k) {
            if (!bakeKindIcon(c, k, static_cast<FileKind>(k))) {
                spdlog::warn("file thumbs: kind icon {} failed", kKindIcon[k]);
            }
        }
        c.kindsReady = true;
        c.dirty = true;
    }
    uint32_t first = 0;
    uint32_t live = 0;
    guiHostListWindow(e.gui, browserGridPath(), first, live);
    uint32_t budget = kBakePerFrame;
    for (uint32_t i = first; i < first + live && i < b.files.size(); ++i) {
        BrowserItem& item = b.files[i];
        if (item.cell != 0) {
            if (item.cell > kFileKinds) {
                c.used[item.cell - 1u] = ++c.clock;
            }
            continue;
        }
        const uint8_t kindCell = static_cast<uint8_t>(static_cast<uint32_t>(item.kind) + 1u);
        if (item.kind != FileKind::Texture && item.kind != FileKind::Material) {
            setThumb(e, i, kindCell);
            continue;
        }
        const std::string full = item.path.string();
        const uint64_t key = hashPath(full.c_str());
        uint32_t cell = kThumbCells;
        for (uint32_t k = kFileKinds; k < kThumbCells; ++k) {
            if (c.key[k] == key && c.stamp[k] == item.mtime) {
                cell = k;
                break;
            }
        }
        if (cell == kThumbCells) {
            if (budget == 0) {
                guiHostSetListThumb(e.gui, browserGridPath(), i, kindCell); // its kind icon until a later frame bakes the preview
                continue;
            }
            --budget;
            cell = pickCell(c);
            for (uint32_t j = 0; j < b.files.size(); ++j) { // the evicted preview's tiles fall back to re-resolving
                if (b.files[j].cell == cell + 1u) {
                    setThumb(e, j, 0);
                }
            }
            const bool ok = item.kind == FileKind::Texture ? bakeTexture(c, cell, full.c_str()) : bakeMaterial(c, cell, full.c_str());
            if (!ok) {
                setThumb(e, i, kindCell);
                continue;
            }
            c.key[cell] = key;
            c.stamp[cell] = item.mtime;
            c.dirty = true;
        }
        c.used[cell] = ++c.clock;
        setThumb(e, i, static_cast<uint8_t>(cell + 1u));
    }
    if (c.dirty && canvasUploadPhoto(*canvas, kThumbSlot, c.atlas.get(), kThumbGrid * kThumbPx, kThumbGrid * kThumbPx)) {
        c.dirty = false;
    }
}

void thumbsInvalidate(Engine& e, const char* path) {
    BrowserState& b = e.browser;
    const uint64_t key = path != nullptr ? hashPath(path) : 0;
    for (uint32_t k = kFileKinds; k < kThumbCells && key != 0; ++k) {
        if (b.thumbs.key[k] == key) {
            b.thumbs.key[k] = 0;
            for (uint32_t j = 0; j < b.files.size(); ++j) {
                if (b.files[j].cell == k + 1u) {
                    setThumb(e, j, 0);
                }
            }
        }
    }
}

} // namespace burnhope

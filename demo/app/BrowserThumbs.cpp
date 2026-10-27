#include "app/BrowserThumbs.hpp"

#include "app/BrowserController.hpp"
#include "app/BrowserThumbsInternal.hpp"
#include "asset/MaterialBake.hpp"
#include "image/ImageDecode.hpp"
#include "gui/GuiHost.hpp"
#include "gui/GuiIcons.hpp"
#include "platform/PlatformUtil.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace burnhope {
namespace {

constexpr uint32_t kBakePerFrame = 3;
// Least recently used preview cell (kind cells are never evicted).
uint32_t pickCell(const ThumbCache& c) {
    uint32_t best = kFileKinds;
    for (uint32_t i = kFileKinds; i < kThumbEnd; ++i) {
        if (c.key[i] == 0) {
            return i;
        }
        if (c.used[i] < c.used[best]) {
            best = i;
        }
    }
    return best;
}

void setThumb(BrowserState& br, uint32_t index, uint8_t cell) {
    br.files[index].cell = cell;
    guiHostSetListThumb(br.gui, browserGridPath(), index, cell);
}

// A free or least recently used preview cell; the tiles that showed an evicted preview re-resolve.
uint32_t claimCell(BrowserState& br) {
    const uint32_t cell = pickCell(br.thumbs);
    for (uint32_t j = 0; j < br.files.size(); ++j) {
        if (br.files[j].cell == cell + 1u) {
            setThumb(br, j, 0);
        }
    }
    return cell;
}

} // namespace

bool thumbsInit(ThumbCache& c, JobPool& pool, const GuiHost* gui) {
    if (!guiHostColor8(gui, "item", c.plate)) {
        return false;
    }
    c.atlas.assign(static_cast<size_t>(kThumbGrid) * kThumbPx * kThumbGrid * kThumbPx * 4u, 0);
    c.matAtlas.assign(static_cast<size_t>(kMatAtlasW) * kMatAtlasH * 4u, 0);
    c.matPreview.assign(static_cast<size_t>(kMatSphereSide) * kMatSphereSide * 4u, 0);
    c.baker.reset(assetModelBakerCreate(pool, c.plate));
    return c.baker != nullptr;
}

void thumbsSync(BrowserState& br) {
    BrowserState& b = br;
    ThumbCache& c = b.thumbs;
    Canvas* canvas = br.canvas;
    if (canvas == nullptr || c.atlas.empty()) {
        return;
    }
    if (!c.kindsReady) {
        for (uint32_t k = 0; k < kFileKinds; ++k) {
            if (!thumbBakeKindIcon(c, k, static_cast<FileKind>(k))) {
                spdlog::warn("file thumbs: kind icon {} failed", k);
            }
        }
        c.kindsReady = true;
        c.dirty = true;
    }
    uint32_t first = 0;
    uint32_t live = 0;
    guiHostListWindow(br.gui, browserGridPath(), first, live);
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
        if (item.kind != FileKind::Texture && item.kind != FileKind::Material && item.kind != FileKind::Model) {
            setThumb(br, i, kindCell);
            continue;
        }
        const std::string full = item.path.string();
        const uint64_t key = thumbHashPath(full.c_str());
        uint32_t cell = kThumbEnd;
        for (uint32_t k = kFileKinds; k < kThumbEnd; ++k) {
            if (c.key[k] == key && c.stamp[k] == item.mtime) {
                cell = k;
                break;
            }
        }
        if (cell == kThumbEnd) {
            bool ok = true;
            if (item.kind == FileKind::Model) {
                uint8_t pixels[kModelThumbBytes];
                if (c.baker == nullptr || assetFetchModelThumb(*c.baker, b.root, full.c_str(), key, item.mtime, pixels) == ModelThumbState::Pending) {
                    guiHostSetListThumb(br.gui, browserGridPath(), i, kindCell); // its kind icon until the background bake lands
                    continue;
                }
                cell = claimCell(br);
                thumbCopyCell(c, cell, pixels);
            } else {
                if (budget == 0) {
                    guiHostSetListThumb(br.gui, browserGridPath(), i, kindCell); // its kind icon until a later frame bakes the preview
                    continue;
                }
                --budget;
                cell = claimCell(br);
                ok = item.kind == FileKind::Texture ? thumbBakeTexture(c, cell, full.c_str()) : thumbBakeMaterial(c, cell, full.c_str());
            }
            if (!ok) {
                setThumb(br, i, kindCell);
                continue;
            }
            c.key[cell] = key;
            c.stamp[cell] = item.mtime;
            c.dirty = true;
        }
        c.used[cell] = ++c.clock;
        setThumb(br, i, static_cast<uint8_t>(cell + 1u));
    }
    if (c.dirty && canvasUploadPhoto(*canvas, kThumbSlot, c.atlas.data(), kThumbGrid * kThumbPx, kThumbGrid * kThumbPx)) {
        c.dirty = false;
    }
}

uint8_t thumbsTextureCell(BrowserState& br, const char* path, uint32_t& budget) {
    ThumbCache& c = br.thumbs;
    const uint8_t kindCell = static_cast<uint8_t>(static_cast<uint32_t>(FileKind::Texture) + 1u);
    if (c.atlas.empty() || !c.kindsReady) {
        return 0;
    }
    const uint64_t key = thumbHashPath(path);
    for (uint32_t k = kFileKinds; k < kThumbEnd; ++k) {
        if (c.key[k] == key) {
            c.used[k] = ++c.clock;
            return static_cast<uint8_t>(k + 1u);
        }
    }
    if (budget == 0) {
        return 0;
    }
    --budget;
    const uint32_t cell = claimCell(br);
    if (!thumbBakeTexture(c, cell, path)) {
        return kindCell;
    }
    std::error_code ec;
    const auto stamp = std::filesystem::last_write_time(path, ec);
    c.key[cell] = key;
    c.stamp[cell] = ec ? 0 : static_cast<int64_t>(stamp.time_since_epoch().count());
    c.used[cell] = ++c.clock;
    c.dirty = true;
    return static_cast<uint8_t>(cell + 1u);
}

void thumbsInvalidate(BrowserState& br, const char* path) {
    BrowserState& b = br;
    const uint64_t key = path != nullptr ? thumbHashPath(path) : 0;
    for (uint32_t k = kFileKinds; k < kThumbEnd && key != 0; ++k) {
        if (b.thumbs.key[k] == key) {
            b.thumbs.key[k] = 0;
            for (uint32_t j = 0; j < b.files.size(); ++j) {
                if (b.files[j].cell == k + 1u) {
                    setThumb(br, j, 0);
                }
            }
        }
    }
}

} // namespace burnhope

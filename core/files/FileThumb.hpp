#pragma once

#include <cstdint>

namespace burnhope {

// Material and image thumbnails share one photo slot laid out as an 8 x 8 atlas of 64 px cells.
constexpr uint8_t kThumbCols = 8;
constexpr uint8_t kThumbCells = kThumbCols * kThumbCols;
constexpr uint32_t kThumbPx = 64;
constexpr uint8_t kThumbNone = 255;
// New bakes allowed per frame; the rest of the visible cells wait for later frames.
constexpr uint8_t kThumbPerFrame = 2;
// Stamp of a cell whose file changed: it still shows the old pixels until the rebake gets a budget slot.
constexpr uint64_t kThumbStale = ~0ull;

// Renders the preview of the material or image at `path` into atlas cell `cell`. False when it cannot be read.
using FileThumbFn = bool (*)(void* user, const char* path, uint8_t cell);

// Fixed-size cache keyed by path hash and file modification time. A cell is baked once and again only when
// the file is new, its timestamp changed or it was invalidated; the least recently used cell is recycled when
// all 64 are taken. At most kThumbPerFrame cells bake between two thumbFrame calls.
struct FileThumbs {
    FileThumbFn bake = nullptr;
    void* user = nullptr;
    uint8_t slot = kThumbNone;
    uint8_t ok[kThumbCells]{};
    uint64_t key[kThumbCells]{};
    uint64_t stamp[kThumbCells]{};
    uint32_t used[kThumbCells]{};
    uint32_t clock = 0;
    uint8_t budget = kThumbPerFrame;
    uint8_t pending = 0;
};

// Atlas cell holding the current thumbnail of `path`, baking it when needed. kThumbNone when unavailable.
uint8_t thumbCell(FileThumbs& cache, const char* path, uint64_t mtime);
void thumbReset(FileThumbs& cache);
// Starts a frame: refills the bake budget. True when cells were deferred last frame and a rebind is due.
bool thumbFrame(FileThumbs& cache);
// Marks the cell of `path` stale (every cell when `path` is not cached under that spelling) so it rebakes.
void thumbInvalidate(FileThumbs& cache, const char* path);

} // namespace burnhope

#pragma once

#include <cstdint>

namespace burnhope {

// Material and image thumbnails share one photo slot laid out as an 8 x 8 atlas of 64 px cells.
constexpr uint8_t kThumbCols = 8;
constexpr uint8_t kThumbCells = kThumbCols * kThumbCols;
constexpr uint32_t kThumbPx = 64;
constexpr uint8_t kThumbNone = 255;

// Renders the preview of the material or image at `path` into atlas cell `cell`. False when it cannot be read.
using FileThumbFn = bool (*)(void* user, const char* path, uint8_t cell);

// Fixed-size cache keyed by path hash and file modification time. A cell is baked once and again only when
// the file is new or its timestamp changed; the least recently used cell is recycled when all 64 are taken.
struct FileThumbs {
    FileThumbFn bake = nullptr;
    void* user = nullptr;
    uint8_t slot = kThumbNone;
    uint8_t ok[kThumbCells]{};
    uint64_t key[kThumbCells]{};
    uint64_t stamp[kThumbCells]{};
    uint32_t used[kThumbCells]{};
    uint32_t clock = 0;
};

// Atlas cell holding the current thumbnail of `path`, baking it when needed. kThumbNone when unavailable.
uint8_t thumbCell(FileThumbs& cache, const char* path, uint64_t mtime);
void thumbReset(FileThumbs& cache);

} // namespace burnhope

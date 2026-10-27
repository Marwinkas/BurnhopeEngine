#pragma once

#include <cstdint>
#include <vector>

#include <memory>

#include "app/BrowserModel.hpp"
#include "app/BrowserModelThumb.hpp"

namespace burnhope {

struct Engine;

constexpr uint8_t kThumbSlot = 10;   // photo slot of the thumbnail atlas
constexpr uint32_t kThumbPx = 64;    // texels per cell
constexpr uint32_t kThumbGrid = 16;  // cells per atlas edge
constexpr uint32_t kThumbCells = kThumbGrid * kThumbGrid;
constexpr uint32_t kThumbEnd = kThumbCells - 1; // a cell is stored as 1 + index in a uint8_t, so the last one stays unused
constexpr uint8_t kThumbPlate[3] = {38, 40, 48}; // #262830 behind every preview
static_assert(kThumbPx == kModelThumbPx);

// 16 x 16 cells of 64 px: cells 0..5 hold one icon per FileKind, the rest an LRU of baked texture / material / model previews.
struct ThumbCache {
    std::vector<uint8_t> atlas;
    std::vector<uint8_t> matAtlas;   // scratch for the material baker
    std::vector<uint8_t> matPreview; // scratch for the material baker
    uint64_t key[kThumbCells]{};
    int64_t stamp[kThumbCells]{};
    uint32_t used[kThumbCells]{};
    uint32_t clock = 0;
    std::unique_ptr<ModelBaker, ModelBakerDelete> baker; // 3D model previews are baked off the frame loop
    bool kindsReady = false;
    bool dirty = false;
};

[[nodiscard]] bool thumbsInit(ThumbCache& cache);
// Gives every shown tile a thumbnail cell (baking at most a few previews per call), then uploads the atlas when it changed.
void thumbsSync(Engine& e);
// Forgets the cached preview of `path` (after it was edited); its tile re-bakes when next shown.
void thumbsInvalidate(Engine& e, const char* path);

} // namespace burnhope

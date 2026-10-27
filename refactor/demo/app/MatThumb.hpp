#pragma once

#include "files/FileThumb.hpp"

#include <cstdint>
#include <vector>

namespace burnhope {

struct Canvas;

// File-browser material previews: one 512 x 512 RGBA atlas of 64 px PBR spheres shown through photo slot 10.
struct MatThumbs {
    Canvas* canvas = nullptr;
    std::vector<uint8_t> atlas;
    std::vector<uint8_t> maps;
    std::vector<uint8_t> shaded;
    uint8_t dirty = 0;
};

constexpr uint8_t kMatThumbSlot = 10;

// Registers the baker with the file browser of `canvas`.
void matThumbsAttach(MatThumbs& thumbs, Canvas& canvas);
// Uploads the atlas once after any cells changed this frame.
void matThumbsFlush(MatThumbs& thumbs);

} // namespace burnhope

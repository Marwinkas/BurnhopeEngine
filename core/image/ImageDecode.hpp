#pragma once

#include <cstddef>
#include <cstdint>

namespace burnhope {

constexpr uint32_t kImageFrames = 16;

struct ImageCpu {
    uint8_t* rgba = nullptr;
    float* rgb = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 1;
    uint8_t hdr = 0;
    uint16_t delayMs[kImageFrames]{};
};

void imageCpuFree(ImageCpu& img);

// Still images and GIF. Frames are stacked top to bottom in rgba.
[[nodiscard]] bool imageLoadFile(ImageCpu& img, const char* path);

// Rasterizes the SVG at `path` as a white tint mask, centred inside the cell x cell square at (cellX, cellY) of the
// RGBA8 image `atlas` (row stride atlasW texels), leaving a one texel transparent margin. False when the file is unreadable.
[[nodiscard]] bool imageRasterSvgCell(const char* path, uint8_t* atlas, uint32_t atlasW, uint32_t cellX, uint32_t cellY, uint32_t cell);

// R = AO, G = roughness, B = metal. One PNG the material file can name.
[[nodiscard]] bool imagePackOrm(const char* aoPath, const char* roughPath, const char* metalPath, const char* outPath);

// R = AO, G = roughness, B = metal, A = height. A missing map keeps the constant for that channel.
[[nodiscard]] bool imagePackOrmh(
    const char* aoPath,
    const char* roughPath,
    const char* metalPath,
    const char* heightPath,
    const char* outPath);

enum class ImageBlock : uint8_t {
    Bc1 = 1,
    Bc3 = 3,
    Bc4 = 4,
    Bc5 = 5,
    Bc6h = 6,
    Bc7 = 7,
};

struct ImageMip {
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t offset = 0;
    uint64_t bytes = 0;
};

// CPU view of a mapped DDS. `base` aliases the caller's bytes.
struct DdsView {
    uint32_t width = 0;
    uint32_t height = 0;
    ImageBlock block = ImageBlock::Bc1;
    ImageMip levels[16]{};
    uint32_t levelCount = 0;
    const uint8_t* base = nullptr;
};

[[nodiscard]] bool imageParseDds(const uint8_t* data, size_t size, DdsView& out);

// Expands mip `level` of a parsed DDS to RGBA8 (width * height * 4 bytes in `rgba`). BC1, BC3 and BC5 decode exactly,
// as do the single-subset BC7 modes 4-6. Partitioned BC7 blocks (modes 0-3, 7) are approximated by their mean endpoint colour.
[[nodiscard]] bool imageDecodeDdsLevel(const DdsView& view, uint32_t level, uint8_t* rgba);

void writeBlockDds(
    const char* path,
    uint32_t width,
    uint32_t height,
    uint32_t mips,
    ImageBlock format,
    const uint8_t* data,
    size_t bytes);

} // namespace burnhope

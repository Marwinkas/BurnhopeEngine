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

void writeBlockDds(
    const char* path,
    uint32_t width,
    uint32_t height,
    uint32_t mips,
    ImageBlock format,
    const uint8_t* data,
    size_t bytes);

} // namespace burnhope

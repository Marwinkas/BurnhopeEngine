#pragma once

#include "rhi/GpuImage.hpp"

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

// Opaque -> BC1, alpha -> BC7 (BC3 if BC7 is missing), HDR -> BC6H.
// One mip chain for still images. GIF stays a single stacked mip so frames do not blend.
[[nodiscard]] bool gpuImageUploadRgba(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint32_t frames = 1);

[[nodiscard]] bool gpuImageUploadHdr(
    GpuImage& img,
    Device& d,
    const float* rgb,
    uint32_t width,
    uint32_t height);

} // namespace burnhope

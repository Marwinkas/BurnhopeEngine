#pragma once

#include "image/ImageDecode.hpp"
#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

[[nodiscard]] uint32_t imageBlockBytes(ImageBlock format);

// One 4x4-aligned mip. RGBA8 for SDR formats, float RGB (3 floats) for Bc6h.
// GPU compute first. CPU ISPC if the shader is missing. Synchronous and thread-safe.
[[nodiscard]] bool gpuCompressLevel(
    Device& d,
    ImageBlock format,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint8_t* dst);

// Overlaps one GPU compress with later CPU work. The pipeline joins any in-flight
// level before starting another, so the readback buffer is never shared across threads.
[[nodiscard]] bool gpuCompressLevelDeferred(
    Device& d,
    ImageBlock format,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint8_t* dst);

void gpuCompressJoin(Device& d);

void compressRgbaLevel(const uint8_t* src, uint32_t width, uint32_t height, VkFormat format, uint8_t* dst);
void compressHdrLevel(const float* rgb, uint32_t width, uint32_t height, uint8_t* dst);

[[nodiscard]] uint16_t imageFloatToHalf(float value);

} // namespace burnhope

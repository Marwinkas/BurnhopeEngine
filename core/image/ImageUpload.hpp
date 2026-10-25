#pragma once

#include "image/ImageDecode.hpp"
#include "rhi/GpuImage.hpp"

#include <cstdint>

namespace burnhope {

// Unpacked RGBA8 mip chain. Scene maps use gpuImageUploadPbr. UI photos stay imageUploadUncompressed.
[[nodiscard]] bool gpuImageUploadRgba8(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    bool srgb);

[[nodiscard]] bool imageUploadUncompressed(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height);

// cacheDds, если не nullptr: после сжатия пишет DDS, следующий запуск читает его через imageLoadDds.
[[nodiscard]] bool gpuImageUploadRgba(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    uint32_t frames = 1,
    const char* cacheDds = nullptr);

// Color is BC7 sRGB, a normal is BC5, ORM / rough / metal / AO / height are BC7 UNORM.
// The chain is written to cacheDds for imageLoadDds. Pass srgb on reload only for Color.
enum class PbrTex : uint8_t {
    Color = 0,
    Normal = 1,
    Data = 2,
};

[[nodiscard]] bool gpuImageUploadPbr(
    GpuImage& img,
    Device& d,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height,
    PbrTex kind,
    const char* cacheDds = nullptr);

[[nodiscard]] bool imageLoadDds(GpuImage& img, Device& d, const char* path, bool srgb = false);

[[nodiscard]] bool imageUploadCompressed(
    GpuImage& img,
    Device& d,
    VkFormat format,
    const uint8_t* blocks,
    uint32_t width,
    uint32_t height);

[[nodiscard]] bool gpuImageUploadR8(GpuImage& img, Device& d, const uint8_t* pixels, uint32_t width, uint32_t height);

[[nodiscard]] bool gpuImageUploadHdr(
    GpuImage& img,
    Device& d,
    const float* rgb,
    uint32_t width,
    uint32_t height);

} // namespace burnhope

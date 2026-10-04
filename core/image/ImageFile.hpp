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

// Block format for pictures and future material textures.
// Bc1 albedo without alpha, Bc3 albedo with alpha, Bc4 roughness/height,
// Bc5 normals (RG), Bc7 high-quality RGBA, Bc6h HDR and cubemaps (mode 11).
enum class ImageBlock : uint8_t {
    Bc1 = 1,
    Bc3 = 3,
    Bc4 = 4,
    Bc5 = 5,
    Bc6h = 6,
    Bc7 = 7,
};

[[nodiscard]] uint32_t imageBlockBytes(ImageBlock format);

// One 4x4-aligned mip. RGBA8 for SDR formats, float RGB (3 floats) for Bc6h.
// GPU compute first. CPU ISPC if the shader is missing.
[[nodiscard]] bool gpuCompressLevel(
    Device& d,
    ImageBlock format,
    const void* pixels,
    uint32_t width,
    uint32_t height,
    uint8_t* dst);

// Opaque -> BC1, alpha -> BC7 (BC3 if BC7 is missing), HDR -> BC6H.
// One mip chain for still images. GIF stays a single stacked mip so frames do not blend.
// 1×1 (и любой RGBA8) без BC. Заглушки альбедо, нормали и ORM.
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

[[nodiscard]] bool imageLoadDds(GpuImage& img, Device& d, const char* path);

[[nodiscard]] bool imageUploadCompressed(
    GpuImage& img,
    Device& d,
    VkFormat format,
    const uint8_t* blocks,
    uint32_t width,
    uint32_t height);

[[nodiscard]] bool gpuImageUploadHdr(
    GpuImage& img,
    Device& d,
    const float* rgb,
    uint32_t width,
    uint32_t height);

} // namespace burnhope

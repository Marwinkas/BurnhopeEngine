#pragma once

#include "rhi/Device.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "ui/Primitive.hpp"
#include "ui/Geometry.hpp"
#include "ui/rhi/UiRenderer.hpp"
#include <cstdint>

namespace burnhope {

inline constexpr uint8_t kPhotoFrameCap = 16;

struct UiState;

struct UiImage {
    uint8_t slot = 0;
    uint8_t frames = 1;
    uint8_t frame = 0;
    uint8_t minimap = 0;
    float acc = 0.0f;
    uint16_t delayMs[kPhotoFrameCap]{};
    float viewX = 0.0f;
    float viewY = 0.0f;
    float viewW = 1.0f;
    float viewH = 1.0f;
};

struct PhotoMeta {
    uint8_t frames = 1;
    uint16_t delayMs[kPhotoFrameCap]{};
};

// Загружает изображение с диска, выделяет VMA-текстуру слота kUiDescPhoto + slot,
// записывает дескриптор в кучу и сохраняет метаданные кадров анимации.
[[nodiscard]] bool uiPhotoUpload(
    UiRenderer& rhi,
    Device& device,
    DescriptorHeaps& heaps,
    uint8_t slot,
    const uint8_t* rgba,
    uint32_t width,
    uint32_t height);

[[nodiscard]] bool uiPhotoLoad(
    UiRenderer& rhi,
    Device& device,
    DescriptorHeaps& heaps,
    PhotoMeta& meta,
    uint8_t slot,
    const char* path);

void uiPhotoTick(UiState& s);

uint32_t uiEmitSprite(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox& box,
    const UiImage& image,
    const UiBox* clip,
    float blur,
    float tintR,
    float tintG,
    float tintB);

} // namespace burnhope
#pragma once

#include "rhi/Device.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "ui/Primitive.hpp"
#include "ui/Geometry.hpp"
#include "ui/rhi/UiRenderer.hpp"
#include <cstdint>

namespace burnhope {

struct UiState;

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

} // namespace burnhope
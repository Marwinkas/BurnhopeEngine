#pragma once

#include "rhi/Device.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/UiPrimitive.hpp"
#include "rhi/ui/UiRenderer.hpp"
#include <cstdint>

namespace burnhope {

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

} // namespace burnhope
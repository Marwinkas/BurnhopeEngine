#pragma once

#include "core/rhi/Device.hpp"
#include "core/rhi/DescriptorHeap.hpp"
#include "core/ui/rhi/UiRenderer.hpp"
#include <cstdint>

namespace burnhope {

inline constexpr uint8_t kPhotoSlots = 8;
inline constexpr uint8_t kPhotoFrameCap = 16;

struct PhotoMeta {
    uint8_t frames = 1;
    uint16_t delayMs[kPhotoFrameCap]{};
};

// Загружает изображение с диска, выделяет VMA-текстуру слота kUiDescPhoto + slot,
// записывает дескриптор в кучу и сохраняет метаданные кадров анимации.
[[nodiscard]] bool uiPhotoLoad(
    UiRenderer& rhi,
    Device& device,
    DescriptorHeaps& heaps,
    PhotoMeta& meta,
    uint8_t slot,
    const char* path);

} // namespace burnhope
#pragma once

#include "rhi/ui/UiFont.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

// Генерация SDF-атласа шрифта и загрузка в дескрипторную кучу Vulkan (слот kUiDescFont)
[[nodiscard]] bool fontLoadSdf(
    UiFontAtlas& font,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    const char* path,
    float pixelSize);

// Автоматический поиск и загрузка дефолтного шрифта проекта
[[nodiscard]] bool fontLoadDefaultSdf(
    UiFontAtlas& font,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    float pixelSize);

} // namespace burnhope
#pragma once

#include "core/ui/Typography.hpp"
#include "core/ui/Style.hpp"
#include "core/rhi/DescriptorHeap.hpp"
#include "core/rhi/GpuImage.hpp"
#include "core/rhi/Device.hpp"

#include <cstdint>

namespace burnhope {

// Генерация SDF-атласа шрифта и загрузка в дескрипторную кучу Vulkan (слот kUiDescFont)
[[nodiscard]] bool fontLoadSdf(
    UiFontAtlas& font,
    UiLook& look,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    const char* path,
    float pixelSize);

// Автоматический поиск и загрузка дефолтного шрифта проекта
[[nodiscard]] bool fontLoadDefaultSdf(
    UiFontAtlas& font,
    UiLook& look,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    float pixelSize);

} // namespace burnhope
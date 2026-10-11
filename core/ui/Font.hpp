#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuImage.hpp"
#include "ui/Model.hpp"

namespace burnhope {

// SDF-атлас один раз на шрифт. Кадр только сэмплирует R8.
[[nodiscard]] bool fontLoadSdf(
    UiFontAtlas& font,
    UiLook& look,
    Device& device,
    DescriptorHeaps& heaps,
    GpuImage& atlas,
    const char* path,
    float pixelSize);

} // namespace burnhope

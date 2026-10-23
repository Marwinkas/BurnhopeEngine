#pragma once

#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/Primitive.hpp"
#include "ui/Typography.hpp"

#include <span>

namespace burnhope {

// GPU execution only. No UiState, Yoga, widgets, menus, or files.
struct UiRenderer {
    GpuBuffer prims[2]{};
    GpuImage font{};
    GpuImage photos[kPhotoSlots]{};
    ShaderExt vs{};
    ShaderExt fs{};
    ShaderExt ms{};
    DescriptorHeaps* heaps = nullptr;
    Device* device = nullptr;
    bool nullMesh = false;
    uint32_t primCount = 0;
};

[[nodiscard]] bool uiRendererCreate(UiRenderer& r, Device& d, DescriptorHeaps& heaps);
void uiRendererDestroy(UiRenderer& r, Device& d);
void uiRendererUploadPrims(UiRenderer& r, Device& d, uint32_t flight, std::span<const UiPrimitive> prims, float viewW, float viewH);
void uiRendererUploadFontTable(UiRenderer& r, Device& d, const UiFontAtlas& font);
[[nodiscard]] UiPrimitive* uiRendererPrimDst(UiRenderer& r, uint32_t flight, uint32_t& cap);
void uiRendererRecord(VkCommandBuffer cmd, const UiRenderer& r, const Swapchain& sc, const FrameContext& fc, uint32_t primCount, bool keepColor);

} // namespace burnhope

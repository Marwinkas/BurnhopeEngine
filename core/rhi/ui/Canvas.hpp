#pragma once

#include "platform/Input.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/UiPrimitive.hpp"
#include "rhi/ui/UiFont.hpp"
#include "rhi/ui/UiPhoto.hpp"
#include "rhi/ui/UiRenderer.hpp"

#include <cstdint>

namespace burnhope {

// Window-manager requests a UI layer may raise; the host applies them on the main thread.
struct WindowOps {
    bool close = false;
    bool minimize = false;
    bool toggleMaximize = false;
    int alwaysOnTop = -1;
    float opacity = -1.0f;
};

// What the UI layer reports back to the host after a frame (cursor shape, text-input and relative-mouse requests).
struct CanvasFeedback {
    int cursor = 0;
    bool pointerOver = false;
    bool wantsText = false;
    bool wantsRelative = false;
    WindowOps ops{};
};

// Primitive producer appended to the slot-48 buffer every frame (the bh_gui layer, installed by core/host/GuiHost).
// `input` is already in points, w/h are points. Returns the number of primitives written to dst (at most cap).
using CanvasLayerFn = uint32_t (*)(void* user, const InputFrame& input, float w, float h, UiPrimitive* dst, uint32_t cap, CanvasFeedback& feedback);

// Canvas is the GPU-side frame coordinator: it owns the primitive ring, the SDF font and the photo slots, hands the mapped
// buffer to the installed layer, uploads the result and records the single UI draw.
struct Canvas {
    UiRenderer rhi{};
    UiFontAtlas font{};
    PhotoMeta photoMeta[kPhotoSlots]{};
    Device* device = nullptr;
    DescriptorHeaps* heaps = nullptr;
    CanvasLayerFn layer = nullptr;
    void* layerUser = nullptr;
    CanvasFeedback feedback{};
    float dpi = 1.0f;
    float clear[3]{0.05f, 0.05f, 0.06f};
};

// Base UI font size in pixels (before DPI scaling) shared by every window.
inline constexpr float kCanvasFontPx = 10.0f;

[[nodiscard]] bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps);
void canvasDestroy(Canvas& c, Device& d);

// Runs the layer on the mapped primitive ring of `flight` and uploads the result. No-op without a layer.
void canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, uint32_t flight, bool upload);
// Records the single UI draw for the primitives uploaded by the last canvasConsume.
void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc, bool keepColor = false);

void canvasSetDpi(Canvas& c, float dpi);
void canvasApplyDpi(Canvas& c, float dpi, float previous);

[[nodiscard]] bool canvasPointerOver(const Canvas& c);
[[nodiscard]] bool canvasWantsRelative(const Canvas& c);

[[nodiscard]] bool canvasLoadPhoto(Canvas& c, uint8_t slot, const char* path);
[[nodiscard]] bool canvasUploadPhoto(Canvas& c, uint8_t slot, const uint8_t* rgba, uint32_t width, uint32_t height);
[[nodiscard]] const PhotoMeta* canvasPhotoMeta(const Canvas& c, uint8_t slot);

[[nodiscard]] bool canvasSetFont(Canvas& c, const char* path, float pixelSize);
// Loads the default SDF face, or assets/fonts/Inter-Regular.ttf beside the executable when the default is missing.
[[nodiscard]] bool canvasUseFont(Canvas& c, float pixelSize);
[[nodiscard]] bool canvasHasFont(const Canvas& c);

} // namespace burnhope

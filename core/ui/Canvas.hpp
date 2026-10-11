#pragma once

#include "platform/Window.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/State.hpp"
#include "ui/rhi/UiRenderer.hpp"

#include <memory>

namespace burnhope {

struct PhotoMeta {
    uint8_t frames = 1;
    uint16_t delayMs[16]{};
};

// Layout, input, and the GPU renderer. Widgets are not created here.
struct Canvas {
    std::unique_ptr<UiState> state;
    UiRenderer rhi{};
    PhotoMeta photoMeta[kPhotoSlots]{};
    Device* device = nullptr;
    DescriptorHeaps* heaps = nullptr;
    void (*builder)(Canvas&, void*) = nullptr;
    void* builderUser = nullptr;
};

using UiBuilder = void (*)(Canvas& canvas, void* user);

[[nodiscard]] bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps);
void canvasDestroy(Canvas& c, Device& d);
[[nodiscard]] UiEvent canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, float timeSec, uint32_t flight = 0, bool upload = true);
void canvasSetLook(Canvas& c, const UiLook& look);
[[nodiscard]] UiLook canvasLook(const Canvas& c);
[[nodiscard]] bool canvasWantsText(const Canvas& c);
[[nodiscard]] bool canvasWantsRelative(const Canvas& c);
[[nodiscard]] bool canvasPointerOver(const Canvas& c);
void canvasMaskScene(const Canvas& c, InputFrame& input);
[[nodiscard]] bool canvasFocusBox(const Canvas& c, float& x, float& y, float& w, float& h);
void canvasApplyDpi(Canvas& c, float dpi, float previous);
void canvasSetDpi(Canvas& c, float dpi);
[[nodiscard]] bool canvasMouseClip(const Canvas& c, float& x, float& y, float& w, float& h);
void canvasTakeWindowOps(Canvas& c, WindowOps& out);
[[nodiscard]] bool canvasAddCommand(Canvas& c, const char* name, UiCommandFn fn, void* user);
void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder);
void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user);
void canvasReload(Canvas& c);
void canvasSetClick(Canvas& c, UiClickFn fn, void* user);
void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user);
bool canvasListen(Canvas& c, UiValueFn fn, void* user);
[[nodiscard]] bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path);
[[nodiscard]] int canvasCursor(const Canvas& c);
[[nodiscard]] bool canvasSetFont(Canvas& c, const char* path, float pixelSize);
[[nodiscard]] bool canvasUseFont(Canvas& c, float pixelSize);
bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap);
void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc, bool keepColor = false);

} // namespace burnhope

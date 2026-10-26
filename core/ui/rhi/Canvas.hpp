#pragma once

#include "platform/Input.hpp"
#include "rhi/Swapchain.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "ui/Style.hpp"
#include "ui/Interaction.hpp"
#include "ui/rhi/UiPhoto.hpp"
#include "ui/UiWindowBridge.hpp"
#include "ui/Types.hpp"
#include "ui/rhi/UiRenderer.hpp"

#include <cstdint>

namespace burnhope {

// Canvas — координатор кадра. Сам ничего не рисует: зовёт верстку, ввод, движение и сборку примитивов.
struct Canvas {
    UiState* state = nullptr;
    UiRenderer rhi{};
    PhotoMeta photoMeta[kPhotoSlots]{};
    Device* device = nullptr;
    DescriptorHeaps* heaps = nullptr;
    void (*builder)(Canvas&, void*) = nullptr;
    void* builderUser = nullptr;
};

using UiBuilder = void (*)(Canvas& canvas, void* user);

// 1. Жизненный цикл холста
[[nodiscard]] bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps);
void canvasDestroy(Canvas& c, Device& d);
void canvasReload(Canvas& c);

// 2. Главный конвейер кадра (Layout -> Input -> Motion -> SSBO Batch Upload)
[[nodiscard]] UiEvent canvasConsume(
    Canvas& c,
    Device& d,
    float w,
    float h,
    const InputFrame& input,
    float timeSec,
    uint32_t flight = 0,
    bool upload = true);

// 3. Запись одного вызова отрисовки UI в командный буфер Vulkan
void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc, bool keepColor = false);

// 4. Параметры экрана и темы
void canvasSetDpi(Canvas& c, float dpi);
void canvasApplyDpi(Canvas& c, float dpi, float previous);
void canvasSetLook(Canvas& c, const UiLook& look);
[[nodiscard]] UiLook canvasLook(const Canvas& c);

// 5. Опрос состояния курсора и фокуса
[[nodiscard]] bool canvasPointerOver(const Canvas& c);
[[nodiscard]] bool canvasWantsText(const Canvas& c);
[[nodiscard]] bool canvasWantsRelative(const Canvas& c);
[[nodiscard]] int canvasCursor(const Canvas& c);
[[nodiscard]] bool canvasFocusBox(const Canvas& c, float& x, float& y, float& w, float& h);
[[nodiscard]] bool canvasMouseClip(const Canvas& c, float& x, float& y, float& w, float& h);

// 6. Подписки на события сборки и кликов
void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user);
void canvasSetClick(Canvas& c, UiClickFn fn, void* user);
void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user);
bool canvasListen(Canvas& c, UiValueFn fn, void* user);

// 7. Делегирование ресурсов и фотографий (реализация в UiPhoto)
[[nodiscard]] bool canvasLoadPhoto(Canvas& c, uint8_t slot, const char* path);
[[nodiscard]] bool canvasUploadPhoto(Canvas& c, uint8_t slot, const uint8_t* rgba, uint32_t width, uint32_t height);
[[nodiscard]] const PhotoMeta* canvasPhotoMeta(const Canvas& c, uint8_t slot);
[[nodiscard]] bool canvasSetFont(Canvas& c, const char* path, float pixelSize);
[[nodiscard]] bool canvasUseFont(Canvas& c, float pixelSize);

// 8. Делегаты оконных операций.
void canvasTakeWindowOps(Canvas& c, WindowOps& out);
[[nodiscard]] bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap);
void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder);

} // namespace burnhope
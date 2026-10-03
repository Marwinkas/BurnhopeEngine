#pragma once

#include "platform/Window.hpp"
#include "rhi/DescriptorHeap.hpp"
#include "rhi/GpuBuffer.hpp"
#include "rhi/GpuImage.hpp"
#include "rhi/MeshDraw.hpp"
#include "rhi/ShaderObject.hpp"
#include "rhi/Swapchain.hpp"
#include "ui/Model.hpp"

namespace burnhope {

struct UiState;

// Yoga layout + Flecs columns + one quad draw.
struct PhotoMeta {
    uint8_t frames = 1;
    uint16_t delayMs[16]{};
};

struct Canvas {
    UiState* state = nullptr;
    GpuBuffer prims{};
    GpuImage font{};
    GpuImage photos[kPhotoSlots]{};
    PhotoMeta photoMeta[kPhotoSlots]{};
    Device* device = nullptr;
    DescriptorHeaps* heaps = nullptr;
    ShaderExt vs{};
    ShaderExt fs{};
    ShaderExt ms{};
    bool nullMesh = false;
    uint32_t drawCount = 0;
    void (*builder)(Canvas&, void*) = nullptr;
    void* builderUser = nullptr;
};

using UiBuilder = void (*)(Canvas& canvas, void* user);

[[nodiscard]] bool canvasCreate(Canvas& c, Device& d, DescriptorHeaps& heaps);
void canvasDestroy(Canvas& c, Device& d);
[[nodiscard]] UiEvent canvasConsume(Canvas& c, Device& d, float w, float h, const InputFrame& input, float timeSec);
void canvasSetLook(Canvas& c, const UiLook& look);
[[nodiscard]] UiLook canvasLook(const Canvas& c);
[[nodiscard]] bool canvasWantsText(const Canvas& c);
[[nodiscard]] bool canvasWantsRelative(const Canvas& c);
[[nodiscard]] bool canvasFocusBox(const Canvas& c, float& x, float& y, float& w, float& h);
void canvasApplyDpi(Canvas& c, float dpi, float previous);
void canvasSetDpi(Canvas& c, float dpi);
[[nodiscard]] bool canvasMouseClip(const Canvas& c, float& x, float& y, float& w, float& h);
void canvasTakeWindowOps(Canvas& c, WindowOps& out);
[[nodiscard]] bool canvasAddCommand(Canvas& c, const char* name, UiCommandFn fn, void* user);
void canvasMenuStyle(Canvas& c, float width, float itemH, float pad, float gap);
void canvasChrome(const Canvas& c, float& titleBar, float& resizeBorder);
void canvasBuildSample(Canvas& c);
void canvasBuildColor(Canvas& c);
void canvasSetBook(Canvas& c, ColorBook* book);
[[nodiscard]] uint16_t canvasImportJson(Canvas& c, uint16_t parent, const char* text);
[[nodiscard]] uint16_t canvasFind(const Canvas& c, const char* name);
void canvasBindName(Canvas& c, const char* name, UiClickFn fn, void* user);
[[nodiscard]] uint16_t canvasImportBin(Canvas& c, uint16_t parent, const char* path);
void canvasSetBuilder(Canvas& c, UiBuilder builder, void* user);
void canvasReload(Canvas& c);
uint16_t canvasNode(Canvas& c, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
[[nodiscard]] uint16_t canvasPage(const Canvas& c, int page);
void canvasShow(Canvas& c, uint16_t id, bool visible);
void canvasSetClick(Canvas& c, UiClickFn fn, void* user);
void canvasOnFocus(Canvas& c, UiFocusFn fn, void* user);
void canvasBind(Canvas& c, uint16_t id, UiClickFn fn, void* user);
void canvasFold(Canvas& c, uint16_t id);
bool canvasListen(Canvas& c, UiValueFn fn, void* user);
[[nodiscard]] bool canvasRange(const Canvas& c, uint16_t id, float& value, float& minV, float& maxV);
void canvasSetHeight(Canvas& c, uint16_t id, float h);
void canvasReparent(Canvas& c, uint16_t id, uint16_t parent);
void canvasPlace(Canvas& c, uint16_t id, bool absolute, float x, float y, float w, float h);
void canvasGrow(Canvas& c, uint16_t id, float extraW, float h);
void canvasDock(Canvas& c, uint16_t panel, uint16_t home, uint8_t edges = 15);
void canvasCommitDock(Canvas& c, uint8_t edge);
void canvasSetDrop(Canvas& c, uint16_t id, uint8_t drop);
void canvasOpenOs(Canvas& c);
void canvasOfferFile(Canvas& c, const char* path);
[[nodiscard]] bool canvasLoadImage(Canvas& c, uint8_t slot, const char* path);
[[nodiscard]] uint32_t canvasLoadJsonDoc(Canvas& c, const char* path);
[[nodiscard]] bool canvasSaveJsonDoc(const Canvas& c, const char* path);
[[nodiscard]] int canvasCursor(const Canvas& c);
void canvasSetIcon(Canvas& c, uint16_t id, uint8_t icon);
void canvasSelection(const Canvas& c, uint16_t& id, char* name, uint8_t cap, uint8_t& len);
void canvasBindContext(Canvas& c, uint16_t owner, uint16_t menu);
[[nodiscard]] uint16_t canvasMakeMenu(Canvas& c);
[[nodiscard]] bool canvasAddCommandTo(Canvas& c, uint16_t menu, const char* name, UiCommandFn fn, void* user);
void canvasText(Canvas& c, uint16_t id, const char* text);
void canvasStamp(Canvas& c, uint16_t id, UiToken token);
[[nodiscard]] bool canvasSetFont(Canvas& c, const char* path, float pixelSize);
[[nodiscard]] bool canvasUseFont(Canvas& c, float pixelSize);
void canvasSetPaint(Canvas& c, uint16_t id, float r, float g, float b, float a);
void canvasBorder(Canvas& c, uint16_t id, float width, float r, float g, float b, float radius);
void canvasShadow(Canvas& c, uint16_t id, float shadow);
void canvasTransform(Canvas& c, uint16_t id, float angle, float scaleX, float scaleY);
void canvasFilter(Canvas& c, uint16_t id, float bright, float contrast);
void canvasTextStyle(Canvas& c, uint16_t id, uint8_t align, uint8_t ellipsis, uint8_t valign, uint8_t wrap, uint8_t deco, float leading);
[[nodiscard]] uint16_t canvasTextId(const Canvas& c, uint16_t id);
[[nodiscard]] uint16_t canvasIcon(Canvas& c, uint16_t parent, uint8_t slot, float w, float h);
void canvasPad(Canvas& c, uint16_t id, float left, float right, float top, float bottom);
void canvasHover(Canvas& c, uint16_t id, float r, float g, float b);
void canvasDisable(Canvas& c, uint16_t id, bool disabled);
void canvasField(Canvas& c, uint16_t id, const char* text, uint8_t filter, bool clear);
void canvasFieldExtra(Canvas& c, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password);
void canvasCheck(Canvas& c, uint16_t id, uint8_t kind, uint8_t group, bool on);
bool canvasTakeClip(Canvas& c, char* dst, uint8_t cap);
void canvasAnimate(Canvas& c, uint16_t id, float r, float g, float b, float a);
void canvasRecord(VkCommandBuffer cmd, const Canvas& c, const Swapchain& sc, const FrameContext& fc);

} // namespace burnhope

#include "gui/Gui.hpp"

#include <cstddef>
#include <cstring>

#include "gui/interaction/Interact.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/layout/GuiScissor.hpp"
#include "gui/overlay/GuiOverlay.hpp"
#include "gui/render/GuiEmitter.hpp"
#include "platform/Check.hpp"

namespace burnhope::gui {
namespace {

// Mirrors UiGpuHeader (rhi/UiPrimitive.hpp), the first 16 bytes of the 64-byte slot-48 header.
struct GuiGpuHeader {
    float extentX;
    float extentY;
    uint32_t count;
    uint32_t pad;
};
static_assert(sizeof(GuiGpuHeader) <= kUiHeaderBytes);

void registerComponents(flecs::world& w) {
    w.component<UiFlex>();
    w.component<UiBox>();
    w.component<UiClip>();
    w.component<UiPaint>();
    w.component<UiText>();
    w.component<UiClass>();
    w.component<UiScroll>();
    w.component<UiDraggable>();
    w.component<UiDragState>();
    w.component<UiClickable>();
    w.component<UiEditable>();
    w.component<UiVirtual>();
    w.component<UiVirtualItem>();
    w.component<UiOverlay>();
    w.component<UiToggle>();
    w.component<GuiVirtualSlots>();
    w.component<GuiTextMetrics>();
    w.component<UiHoverable>();
    w.component<UiHovered>();
    w.component<UiPressed>();
    w.component<UiFocusable>();
    w.component<UiFocused>();
    w.component<UiScrollable>();
    w.component<UiClipChildren>();
    w.component<UiHidden>();
    w.component<UiRoot>();
}

} // namespace

bool guiInit(GuiContext& ctx, flecs::world& ecs) {
    BH_ASSERT(ctx.style != nullptr, "guiInit needs ctx.style");
    if (ctx.style == nullptr) {
        return false;
    }
    ctx.ecs = &ecs;
    if (ctx.style->ready == 0) {
        guiStyleInit(*ctx.style);
    }
    registerComponents(ecs);
    if (!guiLayoutInit(ecs)) {
        ctx.ecs = nullptr;
        return false;
    }
    ecs.set<GuiTextMetrics>(ctx.style->text);
    ctx.interact = GuiInteractState{};
    ctx.primCount = 0;
    return true;
}

void guiConsumeInput(GuiContext& ctx, const InputFrame& in) {
    BH_ASSERT(ctx.ecs != nullptr && ctx.style != nullptr, "guiConsumeInput before guiInit");
    if (ctx.ecs == nullptr || ctx.style == nullptr) {
        return;
    }
    ctx.in = in;
    guiInteractRun(*ctx.ecs, in, ctx.interact, *ctx.style, ctx.clipboard);
}

void guiLayout(GuiContext& ctx, float screenW, float screenH) {
    BH_ASSERT(ctx.ecs != nullptr, "guiLayout before guiInit");
    if (ctx.ecs == nullptr) {
        return;
    }
    flecs::world& w = *ctx.ecs;
    ctx.screenW = screenW;
    ctx.screenH = screenH;
    guiOverlayRun(w, ctx.in, ctx.interact, screenW, screenH);
    guiLayoutRun(w, screenW, screenH);
    guiScissorRun(w, screenW, screenH);
    if (guiVirtualRun(w, ctx.virtualBind, ctx.virtualUser, &ctx.interact) != 0) {
        guiLayoutRun(w, screenW, screenH);
        guiScissorRun(w, screenW, screenH);
    }
}

void guiRender(GuiContext& ctx, [[maybe_unused]] VkCommandBuffer cmd) {
    ctx.primCount = 0;
    if (ctx.ecs == nullptr || ctx.style == nullptr || ctx.prims == nullptr || ctx.primCap == 0) {
        return;
    }
    ctx.primCount = guiEmit(*ctx.ecs, *ctx.style, ctx.prims, ctx.primCap, &ctx.interact);
    if (ctx.writeHeader) {
        const GuiGpuHeader header{ctx.screenW, ctx.screenH, ctx.primCount, 0};
        std::memcpy(reinterpret_cast<std::byte*>(ctx.prims) - kUiHeaderBytes, &header, sizeof(header));
    }
}

void guiSetPrimBuffer(GuiContext& ctx, UiPrimitive* mapped, uint32_t cap) {
    ctx.prims = mapped;
    ctx.primCap = mapped != nullptr ? cap : 0;
    ctx.primCount = 0;
}

} // namespace burnhope::gui

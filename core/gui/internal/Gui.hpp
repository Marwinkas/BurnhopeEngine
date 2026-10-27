#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h> // VkCommandBuffer handle type only; this module never links Vulkan

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "interaction/InteractTextEdit.hpp"
#include "overlay/GuiTooltip.hpp"
#include "style/GuiStyleSheet.hpp"
#include "virtual/GuiVirtualPool.hpp"
#include "platform/Input.hpp"
#include "rhi/UiPrimitive.hpp"

namespace burnhope::gui {

// One GUI instance: a flecs world holding the widget tree plus the per-frame state around it.
// Ownership: the context owns nothing heap-allocated. The world, the style sheet (GuiStyleSheet is ~70 KB, so it is held
// by pointer and must be a long-lived caller object) and the mapped primitive buffer are provided by the host.
struct GuiContext {
    flecs::world* ecs = nullptr;
    GuiStyleSheet* style = nullptr;
    GuiInteractState interact = {};
    GuiTooltipState tooltip = {};
    UiPrimitive* prims = nullptr; // first primitive of the host-mapped slot-48 buffer; the 64-byte header sits right before it
    uint32_t primCap = 0;
    uint32_t primCount = 0;
    bool writeHeader = true; // false when the host shares the slot-48 buffer and writes the one header for all producers
    float screenW = 0.0f;
    float screenH = 0.0f;
    InputFrame in = {};
    const GuiClipboard* clipboard = nullptr;  // optional copy/cut sink
    GuiVirtualBind virtualBind = nullptr;     // fills recycled virtual-list items
    void* virtualUser = nullptr;
};

// Prepares a context: `ctx.style` must already point at a sheet. Initialises the sheet when it is not ready, creates the
// layout state, installs the sheet's text metrics as the world's GuiTextMetrics singleton (used by layout measuring) and
// registers every gui component. Returns false (BH_ASSERT in debug) when ecs or style is missing.
bool guiInit(GuiContext& ctx, flecs::world& ecs);

// Frame step 1 (main thread): stores the input and runs hit-test/hover/focus/click/drag/scroll/text-edit on the layout
// of the previous frame. Actions land in ctx.interact.actions until the next call.
void guiConsumeInput(GuiContext& ctx, const InputFrame& in);

// Frame step 2: overlay placement -> Yoga layout -> scissor -> virtual pool; when the pool changed anything the layout and
// scissor run once more so the recycled items are positioned in the same frame.
void guiLayout(GuiContext& ctx, float screenW, float screenH);

// Frame step 3: emits the whole tree into the primitive buffer (ctx.primCount) and, when ctx.writeHeader is set, writes the
// slot-48 header {screenW, screenH, count, 0} in front of it. Recording the draw needs the RHI, which this module may not link: the
// host passes the same count to its UiRenderer record call (uiRendererRecord) with its own command buffer, so `cmd` is
// not touched here. Does nothing but zero the count when no buffer was set.
void guiRender(GuiContext& ctx, VkCommandBuffer cmd);

// Sets the mapped primitive array (the host maps slot 48 and passes mapped + kUiHeaderBytes) and its capacity in
// primitives (at most kUiPrimCap for the shared ring).
void guiSetPrimBuffer(GuiContext& ctx, UiPrimitive* mapped, uint32_t cap);

} // namespace burnhope::gui

// Flecs-free POD contracts of the drag & drop engine, shared with the host facade (Layer 3 never sees flecs).
#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope::gui {

// Pixels of travel after a press before a drag & drop gesture goes live.
constexpr float kGuiDragDeadzone = 6.0f;
// Gap between the pointer and the floating ghost node.
constexpr float kGuiDragCursorGap = 16.0f;
constexpr float kGuiGhostH = 26.0f;
constexpr float kGuiGhostMaxW = 240.0f;
constexpr float kGuiGhostPadX = 12.0f;
constexpr uint32_t kGuiDragLabelCap = 96;
constexpr uint32_t kGuiDragBytesCap = 256;

// What a drag carries: a caption for the ghost node plus an opaque string / byte blob owned by the application.
struct GuiDragPayload {
    char label[kGuiDragLabelCap] = {};
    uint8_t bytes[kGuiDragBytesCap] = {};
    uint16_t len = 0;  // used bytes
    uint16_t kind = 0; // application tag
    uint8_t pad0[12] = {};
};

// Result of the drop-target query for the pointer position. kind 0 means "no target"; a non-zero kind is handed back
// unchanged to `drop`. highlight asks the module to outline the rect x/y/w/h (a UiDropTarget box) while the pointer is over it.
struct GuiDropHit {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    uint32_t kind = 0;
    uint32_t id = 0; // application data (a row index...)
    uint8_t highlight = 0;
    uint8_t pad0[7] = {};
};

static_assert(std::is_trivially_copyable_v<GuiDragPayload> && sizeof(GuiDragPayload) % 16 == 0);
static_assert(std::is_trivially_copyable_v<GuiDropHit> && sizeof(GuiDropHit) % 16 == 0);

// Application hooks, all called on the main thread from guiDragDropRun.
struct GuiDragDropOps {
    void* user = nullptr;
    // A left press at x/y: fill `out` and return true when a drag source sits there.
    bool (*pick)(void* user, float x, float y, GuiDragPayload& out) = nullptr;
    // While live, every frame: describe the drop target under x/y (leave `out` zeroed for none).
    void (*target)(void* user, float x, float y, const GuiDragPayload& payload, GuiDropHit& out) = nullptr;
    // The button is released after the gesture went live; `hit.kind == 0` when nothing accepts the drop.
    void (*drop)(void* user, float x, float y, const GuiDragPayload& payload, const GuiDropHit& hit) = nullptr;
};

} // namespace burnhope::gui

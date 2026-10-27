#pragma once

#include <cstdint>
#include <type_traits>

#include "gui/data/GuiComponents.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiActionCap = 64;

// Action ids at or above this value are reserved for the engine itself (text editing).
constexpr uint32_t kGuiActionEngineBase = 0xE0000000u;
constexpr uint32_t kGuiActionEditChanged = kGuiActionEngineBase + 1; // text of a UiEditable changed this frame
constexpr uint32_t kGuiActionEditCommit = kGuiActionEngineBase + 2;  // Enter pressed in a single-line UiEditable
constexpr uint32_t kGuiActionDragEnd = kGuiActionEngineBase + 3;     // a UiDraggable gesture was released

struct GuiAction {
    uint64_t entity = 0;
    uint32_t action = 0;
    uint32_t pad0 = 0;
};

// Cross-frame interaction state of one GUI context. Entity handles are raw flecs ids (0 = none).
// `actions` is rebuilt by guiInteractRun every frame: consumers read it after the run, before the next one.
struct GuiInteractState {
    uint64_t hover = 0;   // hit target (or the captured entity while a drag is live)
    uint64_t under = 0;   // topmost painted/target entity under the pointer (scroll, click and focus start here)
    uint64_t capture = 0; // entity owning the pointer (InteractDrag only)
    uint64_t focus = 0;   // keyboard focus
    uint64_t press = 0;   // clickable pressed with the left button (InteractClick only)
    uint64_t tinted = 0;  // entity whose UiPaint.tint currently carries the hover tint (InteractHover only)
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    uint32_t caretTick = 0; // frames since the last edit/focus change; the emitter blinks the caret from it
    uint32_t actionCount = 0;
    uint8_t overlayOpen = 0; // written by the overlay module
    uint8_t tintPressed = 0;
    uint8_t selecting = 0; // text selection by mouse drag is live (InteractTextEdit only)
    uint8_t pad0 = 0;
    uint32_t pad1 = 0;
    GuiAction actions[kGuiActionCap] = {};
};

static_assert(std::is_trivially_copyable_v<GuiAction> && sizeof(GuiAction) == 16);
static_assert(std::is_trivially_copyable_v<GuiInteractState>);

// Appends an action; returns false (and drops it) when the fixed queue is full.
inline bool guiPushAction(GuiInteractState& state, uint64_t entity, uint32_t action) {
    if (state.actionCount >= kGuiActionCap) {
        return false;
    }
    state.actions[state.actionCount] = GuiAction{.entity = entity, .action = action};
    ++state.actionCount;
    return true;
}

// Entity for a stored id, or an id-0 entity when the id is 0 or no longer alive.
inline flecs::entity guiEntityOf(flecs::world& world, uint64_t id) {
    if (id == 0 || !world.is_alive(static_cast<flecs::entity_t>(id))) {
        return flecs::entity();
    }
    return world.entity(static_cast<flecs::entity_t>(id));
}

} // namespace burnhope::gui

#pragma once

#include <cstdint>
#include <type_traits>

#include "data/GuiComponents.hpp"

namespace burnhope::gui {

constexpr uint32_t kGuiActionCap = 64;

// Action ids at or above this value are reserved for the engine itself (text editing).
constexpr uint32_t kGuiActionEngineBase = 0xE0000000u;
constexpr uint32_t kGuiActionEditChanged = kGuiActionEngineBase + 1; // text of a UiEditable changed this frame
constexpr uint32_t kGuiActionEditCommit = kGuiActionEngineBase + 2;  // Enter pressed in a single-line UiEditable
constexpr uint32_t kGuiActionDragEnd = kGuiActionEngineBase + 3;     // a UiDraggable gesture was released
constexpr uint32_t kGuiActionSecondary = kGuiActionEngineBase + 4;   // right button released over a UiClickable (entity = that clickable)

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
    uint64_t barDrag = 0; // scroll view whose scrollbar thumb is being dragged (InteractScroll only)
    uint64_t tinted = 0;  // entity whose UiPaint.tint currently carries the hover tint (InteractHover only)
    uint64_t split = 0;   // splitter grip being dragged (InteractSplitter only)
    uint64_t tabPress = 0; // tab or tab close button pressed with the left button (InteractDock only)
    uint64_t tabBar = 0;   // tab bar the live tab drag would drop into (0 = none)
    float splitOrigin = 0.0f; // pointer coordinate along the splitter axis at the press
    float splitStart = 0.0f;  // size of the resized sibling at the press
    float tabPressX = 0.0f;
    float tabPressY = 0.0f;
    uint32_t tabDropIndex = 0; // position the dragged tab takes in tabBar
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    float barGrab = 0.0f; // pointer offset inside the grabbed thumb
    uint32_t caretTick = 0; // frames since the last edit/focus change; the emitter blinks the caret from it
    uint32_t actionCount = 0;
    uint8_t overlayOpen = 0; // written by the overlay module
    uint8_t tintPressed = 0;
    uint8_t selecting = 0; // text selection by mouse drag is live (InteractTextEdit only)
    uint8_t cursor = 0;    // window cursor kind the frame asks for (Window.cpp windowSetCursor: 1 EW, 2 NS, 8 move)
    uint8_t tabPhase = 0;  // 0 idle, 1 tab pressed (armed), 2 tab drag live
    uint8_t pad0[2] = {};
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

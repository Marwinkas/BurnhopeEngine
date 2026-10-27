#pragma once

#include <cstdint>
#include <type_traits>

#include "data/GuiComponents.hpp"
#include "gui/GuiDragTypes.hpp"

namespace burnhope::gui {

enum class GuiDragPhase : uint8_t { Idle, Armed, Live };

// Cross-frame state of one drag & drop engine. `ghost` and `box` are raw flecs ids of two floating passive nodes (hidden
// until a gesture is live): the ghost carries the payload label next to the pointer, the box outlines the active target.
struct GuiDragDropState {
    uint64_t ghost = 0;
    uint64_t box = 0;
    GuiDragPayload payload = {};
    GuiDropHit hit = {};
    float pressX = 0.0f;
    float pressY = 0.0f;
    GuiDragPhase phase = GuiDragPhase::Idle;
    uint8_t prevLeft = 0;
    uint8_t pad0[6] = {};
};

static_assert(std::is_trivially_copyable_v<GuiDragDropState>);

[[nodiscard]] inline bool guiDragDropLive(const GuiDragDropState& s) {
    return s.phase == GuiDragPhase::Live;
}

// True from the press on a source until the gesture ends (armed or live).
[[nodiscard]] inline bool guiDragDropBusy(const GuiDragDropState& s) {
    return s.phase != GuiDragPhase::Idle;
}

// The generic drag & drop engine. Per frame, after guiConsumeInput:
//   idle:  a left press asks ops.pick for a payload and arms the gesture.
//   armed: moving more than kGuiDragDeadzone from the press goes live.
//   live:  the ghost node follows the pointer (clamped to screenW x screenH), ops.target picks the drop target and the
//          box node outlines it when it asks for a highlight.
//   release while live calls ops.drop; `cancel` (Escape) or a vanished button ends the gesture silently.
// Both floating nodes are hidden again whenever the gesture ends.
void guiDragDropRun(flecs::world& world, GuiDragDropState& state, const GuiDragDropOps& ops, float px, float py, bool left, bool cancel,
                    float screenW, float screenH);

} // namespace burnhope::gui

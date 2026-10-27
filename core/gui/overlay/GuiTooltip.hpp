#pragma once

#include <cstdint>

#include "gui/data/GuiComponents.hpp"
#include "gui/interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

constexpr float kGuiTooltipDelay = 0.45f; // seconds the pointer rests on a node before its tip shows
constexpr float kGuiTooltipGap = 16.0f;   // distance of the tip from the pointer

// Text of the tip of a pooled list item: `host` is the list entity, `index` the logical item. With out == nullptr the call
// only asks whether this item has a tip. Returns false when it has none.
using GuiTooltipTextFn = bool (*)(void* user, uint64_t host, uint32_t index, char* out, uint32_t cap);

struct GuiTooltipState {
    GuiTooltipTextFn text = nullptr;
    void* user = nullptr;
    uint64_t target = 0; // entity the pointer rests on (0 = none)
    uint32_t index = 0;  // logical item of a pooled target
    int64_t sinceNs = 0; // steady clock at the moment the pointer arrived on the target
    bool shown = false;
};

// Shows the tooltip of the node under the pointer: the UiTooltipText of the node or one of its ancestors, or, for a pooled
// list item, the text `state.text` returns. The popup is the entity with UiTooltipHost (an overlay); it appears after
// kGuiTooltipDelay at the pointer, sized to its text, and the overlay pass clamps it inside the screen. A press, a drag or
// moving to another node hides it. Run BEFORE guiOverlayRun in the frame.
void guiTooltipRun(flecs::world& world, const InputFrame& in, const GuiInteractState& interact, GuiTooltipState& state);

} // namespace burnhope::gui

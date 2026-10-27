#pragma once

#include <cstdint>

#include "gui/data/GuiComponents.hpp"

namespace burnhope::gui {

// Tree traversal in draw order. Roots (UiRoot entities with a Yoga node) are visited by ascending layer
// (plain roots 0, an open UiOverlay root uses UiOverlay.layer + 1; closed overlays are skipped, equal layers keep
// world iteration order). Inside a root the walk is pre-order over the Yoga child order, so a later sibling and any
// descendant are drawn above what came before. UiHidden subtrees are never entered.
enum class GuiWalk : uint8_t { Continue, SkipChildren, Stop };
using GuiWalkFn = GuiWalk (*)(flecs::entity e, void* user);
void guiWalkTree(flecs::world& world, GuiWalkFn fn, void* user);

// True when the entity or any ancestor carries UiHidden.
bool guiIsHidden(flecs::entity e);

// True when the entity can receive pointer/keyboard interaction
// (UiHoverable, UiClickable, UiDraggable, UiScrollable, UiFocusable or UiEditable).
bool guiIsTarget(flecs::entity e);

// Deepest visible entity under (x, y): highest layer first, then last drawn. Only targets are returned; any painted
// box (UiPaint) still occludes whatever is below it, in which case the nearest target ancestor of the occluder wins
// (id-0 entity when there is none). `under`, when given, receives the raw topmost painted/target entity.
flecs::entity guiHitTestEx(flecs::world& world, float x, float y, flecs::entity* under);
flecs::entity guiHitTest(flecs::world& world, float x, float y);

// Nearest entity at or above `e` (following flecs parents) that has component/tag T; id-0 entity when none.
template <typename T>
flecs::entity guiAncestorWith(flecs::entity e) {
    while (e.id() != 0 && e.is_valid() && e.is_alive()) {
        if (e.template has<T>()) {
            return e;
        }
        e = e.parent();
    }
    return flecs::entity();
}

} // namespace burnhope::gui

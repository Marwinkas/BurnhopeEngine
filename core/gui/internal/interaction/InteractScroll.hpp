#pragma once

#include <algorithm>

#include "data/GuiComponents.hpp"
#include "interaction/InteractState.hpp"
#include "platform/Input.hpp"

namespace burnhope::gui {

// Pixels scrolled per wheel notch.
constexpr float kGuiWheelStep = 48.0f;

// Vertical scrollbar of a scroll view: a kGuiBarW wide track on the right edge of the box, shown while the content
// is taller than the view. The hit band is wider than the track so the thumb is easy to grab.
constexpr float kGuiBarW = 4.0f;
constexpr float kGuiBarInset = 2.0f;
constexpr float kGuiBarHitW = 12.0f;
constexpr float kGuiBarMinThumb = 24.0f;

struct GuiBarGeometry {
    float trackX = 0.0f;
    float trackY = 0.0f;
    float trackH = 0.0f;
    float thumbY = 0.0f;
    float thumbH = 0.0f;
    float range = 0.0f; // max scroll offset; 0 = no bar
};

[[nodiscard]] inline GuiBarGeometry guiScrollBar(const UiBox& box, const UiScroll& scroll) {
    GuiBarGeometry g{};
    g.range = scroll.contentH - scroll.viewH;
    if (g.range <= 0.5f || box.h < 2.0f * kGuiBarMinThumb) {
        g.range = 0.0f;
        return g;
    }
    g.trackX = box.x + box.w - kGuiBarW - kGuiBarInset;
    g.trackY = box.y + kGuiBarInset;
    g.trackH = box.h - 2.0f * kGuiBarInset;
    g.thumbH = std::max(kGuiBarMinThumb, g.trackH * scroll.viewH / scroll.contentH);
    const float travel = g.trackH - g.thumbH;
    g.thumbY = g.trackY + travel * std::clamp(scroll.offY / g.range, 0.0f, 1.0f);
    return g;
}

// Left press on the right band of an overflowing scroll view grabs its thumb (a press off the thumb centres it on the
// pointer); the drag then follows the pointer until release. Returns true while the thumb owns the left button, so
// the caller hides that button from the other interaction stages.
[[nodiscard]] bool interactScrollBar(flecs::world& world, const InputFrame& in, GuiInteractState& state);

// Clamps every UiScroll to [0, content - view], then routes the wheel (Shift+wheel scrolls horizontally) to the
// nearest UiScrollable at/above state.under that can still move on the wheel's axis. Ignored during a drag.
void interactScroll(flecs::world& world, const InputFrame& in, GuiInteractState& state);

} // namespace burnhope::gui

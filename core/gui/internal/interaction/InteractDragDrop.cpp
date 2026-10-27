#include "gui/interaction/InteractDragDrop.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "gui/interaction/InteractState.hpp"
#include "gui/layout/GuiLayoutEngine.hpp"
#include "gui/text/GuiTextEngine.hpp"

namespace burnhope::gui {

namespace {

void hide(flecs::world& world, GuiDragDropState& s) {
    const flecs::entity ghost = guiEntityOf(world, s.ghost);
    const flecs::entity box = guiEntityOf(world, s.box);
    if (ghost.id() != 0 && !ghost.has<UiHidden>()) {
        guiSetShown(ghost, false);
    }
    if (box.id() != 0 && !box.has<UiHidden>()) {
        guiSetShown(box, false);
    }
}

void end(flecs::world& world, GuiDragDropState& s) {
    hide(world, s);
    s.phase = GuiDragPhase::Idle;
    s.payload = {};
    s.hit = {};
}

// Caption width of the ghost from the font metrics, clamped to the ghost's maximum.
float ghostWidth(flecs::world& world, const UiText& text) {
    const GuiTextMetrics* m = world.try_get<GuiTextMetrics>();
    const float textW = m != nullptr ? guiTextXFromCaret(*m, std::string_view(text.text, text.len), static_cast<float>(text.size), text.len) : 0.0f;
    return std::min(kGuiGhostMaxW, textW + 2.0f * kGuiGhostPadX);
}

void showGhost(flecs::world& world, GuiDragDropState& s, float px, float py, float screenW, float screenH) {
    flecs::entity ghost = guiEntityOf(world, s.ghost);
    if (ghost.id() == 0) {
        return;
    }
    UiText* text = ghost.try_get_mut<UiText>();
    if (text != nullptr) {
        const size_t n = std::min<size_t>(std::strlen(s.payload.label), kGuiTextCap - 1);
        std::memset(text->text, 0, kGuiTextCap);
        std::memcpy(text->text, s.payload.label, n);
        text->len = static_cast<uint8_t>(n);
    }
    const float w = text != nullptr ? ghostWidth(world, *text) : kGuiGhostMaxW;
    const float x = std::clamp(px + kGuiDragCursorGap, 0.0f, std::max(0.0f, screenW - w));
    const float y = std::clamp(py + kGuiDragCursorGap, 0.0f, std::max(0.0f, screenH - kGuiGhostH));
    guiLayoutSetRect(ghost, x, y, w, kGuiGhostH);
    if (ghost.has<UiHidden>()) {
        guiSetShown(ghost, true);
    }
}

void showBox(flecs::world& world, const GuiDragDropState& s) {
    flecs::entity box = guiEntityOf(world, s.box);
    if (box.id() == 0) {
        return;
    }
    const bool on = s.hit.kind != 0 && s.hit.highlight != 0;
    if (on) {
        guiLayoutSetRect(box, s.hit.x, s.hit.y, s.hit.w, s.hit.h);
    }
    if (box.has<UiHidden>() == on) {
        guiSetShown(box, on);
    }
}

} // namespace

void guiDragDropRun(flecs::world& world, GuiDragDropState& state, const GuiDragDropOps& ops, float px, float py, bool left, bool cancel,
                    float screenW, float screenH) {
    const bool pressed = left && state.prevLeft == 0;
    state.prevLeft = left ? 1 : 0;
    if (state.phase == GuiDragPhase::Idle) {
        if (pressed && ops.pick != nullptr) {
            GuiDragPayload payload{};
            if (ops.pick(ops.user, px, py, payload)) {
                state.payload = payload;
                state.pressX = px;
                state.pressY = py;
                state.phase = GuiDragPhase::Armed;
            }
        }
        return;
    }
    if (cancel || !left) {
        if (!cancel && state.phase == GuiDragPhase::Live && ops.drop != nullptr) {
            ops.drop(ops.user, px, py, state.payload, state.hit);
        }
        end(world, state);
        return;
    }
    if (state.phase == GuiDragPhase::Armed) {
        const float dx = px - state.pressX;
        const float dy = py - state.pressY;
        if (dx * dx + dy * dy <= kGuiDragDeadzone * kGuiDragDeadzone) {
            return;
        }
        state.phase = GuiDragPhase::Live;
    }
    state.hit = {};
    if (ops.target != nullptr) {
        ops.target(ops.user, px, py, state.payload, state.hit);
    }
    showGhost(world, state, px, py, screenW, screenH);
    showBox(world, state);
}

} // namespace burnhope::gui

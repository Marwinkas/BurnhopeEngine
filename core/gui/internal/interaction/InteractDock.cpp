#include "interaction/InteractDock.hpp"

#include <cmath>

#include "gui/GuiDragTypes.hpp"
#include "input/GuiHitTest.hpp"
#include "interaction/InteractSplitter.hpp"
#include "layout/GuiDock.hpp"
#include "style/GuiStyleSheet.hpp"

namespace burnhope::gui {
namespace {

enum Phase : uint8_t { kIdle = 0, kArmed = 1, kLive = 2, kClosePress = 3 };

constexpr float kBarOutlinePx = 1.5f;

void outline(flecs::world& world, uint64_t bar, bool on) {
    const flecs::entity e = guiEntityOf(world, bar);
    UiPaint* paint = e.id() != 0 ? e.try_get_mut<UiPaint>() : nullptr;
    if (paint != nullptr) {
        paint->borderW = on ? kBarOutlinePx : 0.0f;
        paint->borderToken = static_cast<uint8_t>(kTokAccent);
    }
}

void setBar(flecs::world& world, GuiInteractState& state, uint64_t bar) {
    if (state.tabBar != bar) {
        outline(world, state.tabBar, false);
        state.tabBar = bar;
    }
    outline(world, bar, bar != 0);
}

void finish(flecs::world& world, GuiInteractState& state) {
    setBar(world, state, 0);
    state.tabPress = 0;
    state.tabPhase = kIdle;
}

flecs::entity tabOf(flecs::entity e) {
    if (e.id() == 0) {
        return flecs::entity();
    }
    if (e.has<UiTabClose>()) {
        return e.parent();
    }
    return e.has<UiTab>() ? e : flecs::entity();
}

} // namespace

void interactDock(flecs::world& world, const InputFrame& in, GuiInteractState& state) {
    const bool down = (in.pointer.down & kPointerLeft) != 0;
    const flecs::entity under = guiEntityOf(world, state.under);

    if (state.tabPhase == kIdle) {
        if ((in.pointer.pressed & kPointerLeft) == 0 || state.capture != 0 || state.split != 0) {
            return;
        }
        const flecs::entity tab = tabOf(under);
        const UiTab* t = tab.id() != 0 ? tab.try_get<UiTab>() : nullptr;
        if (t == nullptr) {
            return;
        }
        state.tabPress = under.id();
        state.tabPressX = in.pointer.x;
        state.tabPressY = in.pointer.y;
        if (under.has<UiTabClose>()) {
            state.tabPhase = kClosePress;
        } else {
            guiDockSelect(guiEntityOf(world, t->panel));
            state.tabPhase = kArmed;
        }
        return;
    }

    const flecs::entity pressed = guiEntityOf(world, state.tabPress);
    if (pressed.id() == 0 || in.escape != 0) {
        finish(world, state);
        return;
    }
    if (state.tabPhase == kClosePress) {
        if (!down) {
            if (under == pressed) {
                const UiTab* t = pressed.parent().try_get<UiTab>();
                if (t != nullptr) {
                    guiDockClose(guiEntityOf(world, t->panel));
                }
            }
            finish(world, state);
        }
        return;
    }
    if (state.tabPhase == kArmed) {
        const float dx = in.pointer.x - state.tabPressX;
        const float dy = in.pointer.y - state.tabPressY;
        if (!down) {
            finish(world, state);
            return;
        }
        if (dx * dx + dy * dy <= kGuiDragDeadzone * kGuiDragDeadzone) {
            return;
        }
        state.tabPhase = kLive;
    }
    state.cursor = kGuiCursorMove;
    const flecs::entity bar = guiAncestorWith<UiTabBar>(under);
    if (down) {
        setBar(world, state, bar.id());
        if (bar.id() != 0) {
            state.tabDropIndex = guiDockDropIndex(world, pressed, bar, in.pointer.x);
        }
        return;
    }
    if (bar.id() != 0) {
        (void)guiDockMoveTab(world, pressed, bar, guiDockDropIndex(world, pressed, bar, in.pointer.x));
    }
    finish(world, state);
}

} // namespace burnhope::gui

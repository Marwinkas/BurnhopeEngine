#include "overlay/GuiTooltip.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

#include <yoga/Yoga.h>

#include "text/GuiTextEngine.hpp"
#include "virtual/GuiVirtualPool.hpp"

namespace burnhope::gui {
namespace {

constexpr uint32_t kAncestorCap = 6; // ancestors searched for a UiTooltipText

struct TipQuery {
    flecs::query<> hosts;
};

flecs::entity hostOf(flecs::world& world) {
    if (world.try_get<TipQuery>() == nullptr) {
        world.set<TipQuery>({world.query_builder<>().with<UiTooltipHost>().build()});
    }
    flecs::entity found;
    world.try_get<TipQuery>()->hosts.each([&](flecs::entity e) { found = e; });
    return found;
}

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void hide(flecs::entity host, GuiTooltipState& s) {
    if (s.shown) {
        if (UiOverlay* o = host.try_get_mut<UiOverlay>()) {
            o->open = 0;
        }
        s.shown = false;
    }
}

// Entity the tip belongs to under the pointer: a pooled item of a list with tips, or the nearest node with a UiTooltipText.
bool findTarget(flecs::world& world, const GuiTooltipState& s, uint64_t under, uint64_t& id, uint32_t& index) {
    flecs::entity at = guiEntityOf(world, under);
    for (uint32_t depth = 0; depth < kAncestorCap && at.id() != 0 && at.is_alive(); ++depth, at = at.parent()) {
        if (const UiVirtualItem* item = at.try_get<UiVirtualItem>()) {
            const flecs::entity list = at.parent();
            if (s.text != nullptr && item->index != kGuiVirtualUnbound && list.id() != 0 && s.text(s.user, list.id(), item->index, nullptr, 0)) {
                id = at.id();
                index = item->index;
                return true;
            }
            return false;
        }
        if (at.has<UiTooltipText>()) {
            id = at.id();
            index = kGuiVirtualUnbound;
            return true;
        }
    }
    return false;
}

// Lines the text takes at the width of the popup, so its height follows its content.
float tipHeight(flecs::world& world, flecs::entity host, const UiText& t) {
    const GuiTextMetrics* m = world.try_get<GuiTextMetrics>();
    const UiFlex* flex = host.try_get<UiFlex>();
    if (m == nullptr || flex == nullptr || flex->node == nullptr) {
        return 0.0f;
    }
    const float padX = YGNodeStyleGetPadding(flex->node, YGEdgeLeft).value + YGNodeStyleGetPadding(flex->node, YGEdgeRight).value;
    const float padY = YGNodeStyleGetPadding(flex->node, YGEdgeTop).value + YGNodeStyleGetPadding(flex->node, YGEdgeBottom).value;
    const float px = static_cast<float>(t.size);
    GuiTextLine lines[16];
    const uint32_t count = std::max(1u, std::min(16u, guiTextWrap(*m, std::string_view(t.text, t.len), px, YGNodeStyleGetWidth(flex->node).value - padX, lines, 16)));
    return static_cast<float>(count) * guiTextLineHeight(*m, px) + padY;
}

} // namespace

void guiTooltipRun(flecs::world& world, const InputFrame& in, const GuiInteractState& interact, GuiTooltipState& state) {
    const flecs::entity host = hostOf(world);
    if (!host.is_valid()) {
        return;
    }
    uint64_t id = 0;
    uint32_t index = 0;
    const bool active = in.pointer.down == 0 && interact.capture == 0 && findTarget(world, state, interact.under, id, index);
    if (!active) {
        hide(host, state);
        state.target = 0;
        return;
    }
    if (id != state.target || index != state.index) {
        hide(host, state);
        state.target = id;
        state.index = index;
        state.sinceNs = nowNs();
        return;
    }
    if (state.shown || static_cast<float>(nowNs() - state.sinceNs) * 1e-9f < kGuiTooltipDelay) {
        return;
    }
    UiText* text = host.try_get_mut<UiText>();
    UiOverlay* overlay = host.try_get_mut<UiOverlay>();
    const flecs::entity source = world.entity(id);
    if (text == nullptr || overlay == nullptr) {
        return;
    }
    char buf[kGuiTextCap] = {};
    if (index != kGuiVirtualUnbound) {
        if (!state.text(state.user, source.parent().id(), index, buf, sizeof(buf))) {
            return;
        }
    } else if (const UiTooltipText* fixed = source.try_get<UiTooltipText>()) {
        std::memcpy(buf, fixed->text, sizeof(buf) - 1);
    }
    std::memcpy(text->text, buf, kGuiTextCap);
    text->len = static_cast<uint8_t>(std::min<size_t>(std::strlen(buf), kGuiTextCap - 1));
    if (const UiFlex* flex = host.try_get<UiFlex>(); flex != nullptr && flex->node != nullptr) {
        YGNodeStyleSetHeight(flex->node, tipHeight(world, host, *text));
    }
    overlay->anchorX = in.pointer.x + kGuiTooltipGap;
    overlay->anchorY = in.pointer.y + kGuiTooltipGap;
    overlay->anchorW = 1.0f;
    overlay->anchorH = 1.0f;
    overlay->open = 1;
    state.shown = true;
}

} // namespace burnhope::gui

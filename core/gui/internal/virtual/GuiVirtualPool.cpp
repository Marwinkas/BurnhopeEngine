#include "virtual/GuiVirtualPool.hpp"

#include <algorithm>
#include <cmath>

#include "layout/GuiLayoutEngine.hpp"
#include "platform/Check.hpp"

namespace burnhope::gui {
namespace {

struct GuiVirtualQuery {
    flecs::query<UiVirtual, UiScroll> hosts;
};

const GuiVirtualQuery& virtualQuery(flecs::world& world) {
    if (const GuiVirtualQuery* cached = world.try_get<GuiVirtualQuery>()) {
        return *cached;
    }
    world.set<GuiVirtualQuery>({world.query<UiVirtual, UiScroll>()});
    return *world.try_get<GuiVirtualQuery>();
}

void showItem(flecs::entity item, bool show, uint32_t& changed) {
    const bool hidden = item.has<UiHidden>();
    if (show != hidden) {
        return;
    }
    if (show) {
        item.remove<UiHidden>();
    } else {
        item.add<UiHidden>();
    }
    if (const UiFlex* flex = item.try_get<UiFlex>(); flex != nullptr && flex->node != nullptr) {
        YGNodeStyleSetDisplay(flex->node, show ? YGDisplayFlex : YGDisplayNone);
    }
    ++changed;
}

void placeItem(flecs::entity item, const UiVirtual& v, uint32_t index) {
    const UiFlex* flex = item.try_get<UiFlex>();
    if (flex == nullptr || flex->node == nullptr) {
        return;
    }
    const uint32_t cols = std::max(1u, v.columns);
    YGNodeRef node = flex->node;
    YGNodeStyleSetPositionType(node, YGPositionTypeAbsolute);
    YGNodeStyleSetPosition(node, YGEdgeLeft, static_cast<float>(index % cols) * v.itemW);
    YGNodeStyleSetPosition(node, YGEdgeTop, static_cast<float>(index / cols) * v.itemH);
    if (v.itemW > 0.0f) {
        YGNodeStyleSetWidth(node, v.itemW);
    } else {
        YGNodeStyleSetWidthPercent(node, 100.0f);
    }
    YGNodeStyleSetHeight(node, v.itemH);
}

flecs::entity spawnItem(flecs::world& world, flecs::entity host, uint32_t action) {
    flecs::entity item = world.entity().child_of(host);
    item.set<UiBox>({});
    UiPaint paint{};
    paint.tint[3] = 0.0f; // transparent until the bind callback paints it
    item.set<UiPaint>(paint);
    item.set<UiText>({});
    item.set<UiVirtualItem>({kGuiVirtualUnbound});
    item.add<UiHoverable>();
    if (action != 0) {
        item.set<UiClickable>({action});
    }
    if (!guiLayoutAttach(item)) {
        item.destruct();
        return flecs::entity();
    }
    return item;
}

uint32_t runHost(flecs::world& world, flecs::entity host, GuiVirtualBind bind, void* user, GuiInteractState* interact) {
    if (!host.has<UiVirtual>() || !host.has<UiScroll>()) {
        return 0;
    }
    // Structural changes on the host happen before any component pointer is taken.
    if (!host.has(flecs::OrderedChildren)) {
        host.add(flecs::OrderedChildren);
    }
    if (!host.has<GuiVirtualSlots>()) {
        host.set<GuiVirtualSlots>({});
    }
    UiVirtual* v = host.try_get_mut<UiVirtual>();
    UiScroll* scroll = host.try_get_mut<UiScroll>();
    GuiVirtualSlots* slots = host.try_get_mut<GuiVirtualSlots>();
    BH_ASSERT(v->itemH > 0.0f, "virtual itemH must be positive");
    if (v->itemH <= 0.0f) {
        v->live = 0;
        return 0;
    }

    uint32_t changed = 0;
    const uint32_t cols = std::max(1u, v->columns);
    v->columns = cols;
    const uint32_t rows = (v->count + cols - 1) / cols;
    const float contentH = static_cast<float>(rows) * v->itemH;
    const UiBox* box = host.try_get<UiBox>();
    const float viewH = scroll->viewH > 0.0f ? scroll->viewH : (box != nullptr ? box->h : 0.0f);
    scroll->contentH = contentH;
    if (v->itemW > 0.0f) {
        scroll->contentW = static_cast<float>(cols) * v->itemW;
    }
    const float maxOff = std::max(0.0f, contentH - viewH);
    const float clamped = std::clamp(scroll->offY, 0.0f, maxOff);
    if (clamped != scroll->offY) {
        scroll->offY = clamped;
        ++changed;
    }

    const uint32_t firstRow = static_cast<uint32_t>(std::floor(clamped / v->itemH));
    uint32_t visRows = static_cast<uint32_t>(std::ceil(viewH / v->itemH)) + 1u;
    visRows = std::min(visRows, kGuiVirtualCap / std::min(cols, kGuiVirtualCap));
    const uint32_t first = std::min(firstRow * cols, v->count);
    const uint32_t wanted = std::min(v->count - first, std::min(visRows * cols, kGuiVirtualCap));
    v->first = first;
    v->live = wanted;

    const bool restyle = slots->placedW != v->itemW || slots->placedH != v->itemH || slots->placedCols != cols;
    slots->placedW = v->itemW;
    slots->placedH = v->itemH;
    slots->placedCols = cols;

    bool covered[kGuiVirtualCap] = {};
    for (uint32_t k = 0; k < wanted; ++k) {
        const uint32_t index = first + k;
        const uint32_t s = index % kGuiVirtualCap;
        covered[s] = true;
        flecs::entity item = slots->item[s] != 0 ? world.entity(slots->item[s]) : flecs::entity();
        if (!item.is_valid() || !item.is_alive()) {
            item = spawnItem(world, host, v->itemAction);
            if (!item.is_valid()) {
                slots->item[s] = 0;
                covered[s] = false;
                continue;
            }
            slots->item[s] = item.id();
        }
        showItem(item, true, changed);
        UiVirtualItem* bound = item.try_get_mut<UiVirtualItem>();
        if (bound == nullptr) {
            continue;
        }
        const bool moved = bound->index != index;
        if (moved || restyle) {
            placeItem(item, *v, index);
        }
        if (moved) {
            bound->index = index;
            ++changed;
            // a recycled slot is a different logical item: drop the previous item's transient state
            item.remove<UiHovered>();
            item.remove<UiPressed>();
            item.remove<UiFocused>();
            if (interact != nullptr && interact->focus == item.id()) {
                interact->focus = 0;
            }
            if (bind != nullptr) {
                bind(item, index, user);
            }
        }
    }
    for (uint32_t s = 0; s < kGuiVirtualCap; ++s) {
        if (covered[s] || slots->item[s] == 0) {
            continue;
        }
        flecs::entity item = world.entity(slots->item[s]);
        if (!item.is_alive()) {
            slots->item[s] = 0;
            continue;
        }
        showItem(item, false, changed);
        if (UiVirtualItem* bound = item.try_get_mut<UiVirtualItem>()) {
            bound->index = kGuiVirtualUnbound;
        }
    }
    return changed;
}

} // namespace

uint32_t guiVirtualRun(flecs::world& world, GuiVirtualBind bind, void* user, GuiInteractState* interact) {
    flecs::entity hosts[kGuiVirtualHostCap];
    uint32_t n = 0;
    virtualQuery(world).hosts.each([&](flecs::entity e, UiVirtual&, UiScroll&) {
        if (n < kGuiVirtualHostCap) {
            hosts[n++] = e;
        }
    });
    uint32_t changed = 0;
    for (uint32_t i = 0; i < n; ++i) {
        changed += runHost(world, hosts[i], bind, user, interact);
    }
    return changed;
}

} // namespace burnhope::gui

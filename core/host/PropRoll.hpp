#pragma once

#include "host/PropGrid.hpp"
#include "ui/State.hpp"

namespace burnhope {

// Private to the property grid translation units: the label chain and subtree walks both PropGrid.cpp and
// PropFilter.cpp lean on.
template <typename Fn>
void propEachLabel(UiState& s, Fn&& fn) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    uint16_t id = roll != nullptr ? roll->labels : kUiNone;
    while (id != kUiNone && id < s.count && s.ent[id].is_alive()) {
        const UiPropLabel* lab = s.ent[id].try_get<UiPropLabel>();
        if (lab == nullptr) {
            break;
        }
        const uint16_t next = lab->next;
        if (!fn(id)) {
            break;
        }
        id = next;
    }
}

template <typename Fn>
void propEachChild(const UiState& s, uint16_t parent, Fn&& fn) {
    const uint32_t n = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t child = uiNodeChild(s, parent, i);
        if (child == kUiNone || child >= s.count) {
            continue;
        }
        if (!fn(child)) {
            break;
        }
    }
}

bool propUnder(const UiState& s, uint16_t id, uint16_t host);
uint16_t propBar(const UiState& s);

} // namespace burnhope

#pragma once

#include "host/PropGrid.hpp"

namespace burnhope {

// Private to the property grid translation units: the label chain and subtree walks both PropGrid.cpp and
// PropFilter.cpp lean on.
template <typename State, typename Fn>
void propEachLabel(State& s, Fn&& fn) {
    const UiPropRoll* roll = s.world.template try_get<UiPropRoll>();
    uint16_t id = roll != nullptr ? roll->labels : kUiNone;
    while (id != kUiNone && id < s.count && s.ent[id].is_alive()) {
        const UiPropLabel* lab = s.ent[id].template try_get<UiPropLabel>();
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

template <typename State, typename Fn>
void propEachChild(const State& s, uint16_t parent, Fn&& fn) {
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

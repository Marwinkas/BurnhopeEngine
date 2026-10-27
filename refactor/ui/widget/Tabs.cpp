#include "ui/Panel.hpp"
#include "ui/widget/Tabs.hpp"
#include "ui/State.hpp"

namespace burnhope {

void uiMarkTab(UiState& s, uint16_t strip, uint16_t tab) {
    uint16_t index = 0;
    const uint32_t n = uiNodeChildCount(s, strip);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t id = uiNodeChild(s, strip, i);
        if (id >= s.count || !s.ent[id].has<UiFocusable>()) {
            continue;
        }
        if (id == tab) {
            s.ent[id].set<UiTab>({.index = index});
        } else {
            s.ent[id].remove<UiTab>();
        }
        ++index;
        uiSetActive(s, id, id == tab);
    }
}

} // namespace burnhope

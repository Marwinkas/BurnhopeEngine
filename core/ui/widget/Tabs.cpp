#include "ui/widget/Tabs.hpp"

namespace burnhope {

void uiMarkTab(UiState& s, uint16_t strip, uint16_t tab) {
    for (uint16_t id = 1; id < s.count; ++id) {
        if (s.parentOf[id] != strip || !s.ent[id].has<UiFocusable>()) {
            continue;
        }
        auto* live = s.ent[id].try_get_mut<UiPaint>();
        if (live == nullptr) {
            continue;
        }
        live->token = id == tab ? UiToken::Accent : UiToken::Button;
        uiNotifyVisualDirty(s);
    }
}

} // namespace burnhope

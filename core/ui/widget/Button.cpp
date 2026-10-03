#include "ui/widget/Detail.hpp"

namespace burnhope {

void activate(UiState& s, uint16_t id) {
    const UiPaint* p = s.ent[id].try_get<UiPaint>();
    if (p == nullptr || p->disabled != 0) {
        return;
    }
    if (p->role == static_cast<uint8_t>(UiRole::Check)) {
        auto* check = s.ent[id].try_get_mut<UiCheck>();
        if (check != nullptr) {
            if (check->kind == 1) {
                check->on = 1;
                for (uint16_t other = 0; other < s.count; ++other) {
                    if (other == id) {
                        continue;
                    }
                    auto* mate = s.ent[other].try_get_mut<UiCheck>();
                    if (mate != nullptr && mate->kind == 1 && mate->group == check->group) {
                        mate->on = 0;
                    }
                }
            } else {
                check->on = check->on == 0 ? 1 : 0;
            }
        }
        s.visualDirty = true;
    }
    uiTrace(s, "activate", id);
    if (const UiAction* act = s.ent[id].try_get<UiAction>(); act != nullptr && act->fn != nullptr) {
        act->fn(act->user, id, p->tag);
        return;
    }
    if (p->role == static_cast<uint8_t>(UiRole::Button) && s.onClick != nullptr) {
        s.onClick(s.clickUser, id, p->tag);
    }
}

} // namespace burnhope

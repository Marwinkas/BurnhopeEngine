#include "Str.hpp"
#include "color/Color.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/kit/ColorInternal.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Slider.hpp"

namespace burnhope {

UiColorPickerComponent* uiColorFind(UiState& s) {
    const uint16_t id = uiColorFirst(s, [](const UiColorPickerComponent&) { return true; });
    return id == kUiNone ? nullptr : s.ent[id].try_get_mut<UiColorPickerComponent>();
}

const UiColorPickerComponent* uiColorFind(const UiState& s) {
    const uint16_t id = uiColorFirst(s, [](const UiColorPickerComponent&) { return true; });
    return id == kUiNone ? nullptr : s.ent[id].try_get<UiColorPickerComponent>();
}

void uiColorSetTarget(UiState& s, uint16_t field) {
    if (UiColorPickerComponent* color = uiColorFind(s)) {
        color->colorTarget = field;
    }
}

uint16_t uiColorCollect(const UiState& s, uint16_t* ids, uint16_t cap) {
    uint16_t n = 0;
    s.world.each(s.world.id<UiColorPickerComponent>(), [&](flecs::entity e) {
        const UiSlot* slot = e.try_get<UiSlot>();
        if (slot != nullptr && slot->index < s.count && s.ent[slot->index] == e && n < cap) {
            ids[n++] = slot->index;
        }
    });
    for (uint16_t i = 1; i < n; ++i) {
        const uint16_t id = ids[i];
        uint16_t at = i;
        for (; at > 0 && ids[at - 1] > id; --at) {
            ids[at] = ids[at - 1];
        }
        ids[at] = id;
    }
    return n;
}

const UiColorPickerComponent* uiColorAt(const UiState& s, uint16_t id) {
    return id < s.count ? s.ent[id].try_get<UiColorPickerComponent>() : nullptr;
}

} // namespace burnhope

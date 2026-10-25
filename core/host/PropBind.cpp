#include "host/PropBind.hpp"

#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"

#include <cstdio>

namespace burnhope {

void UiPropBinder::attach(float* scalarBase, uint16_t scalarCount, uint8_t* flagBase, uint16_t flagCount) {
    scalars = scalarBase;
    flags = flagBase;
    scalarN = scalarCount;
    flagN = flagCount;
}

void UiPropBinder::clear() {
    rows.clear();
    shown.clear();
    menus.clear();
    commands.clear();
}

void UiPropBinder::show(uint16_t node, uint16_t flag, uint8_t value, uint8_t op) {
    if (node == kUiNone) {
        return;
    }
    shown.push_back(UiShowRule{.node = node, .flag = flag, .value = value, .op = op});
}

void propApplyShow(UiState& state, const UiPropBinder& bind) {
    for (const UiShowRule& rule : bind.shown) {
        const uint8_t flag = bind.flags != nullptr && rule.flag < bind.flagN ? bind.flags[rule.flag] : 0;
        const bool on = rule.op == 0 ? flag == rule.value : flag != rule.value;
        uiShow(state, rule.node, on);
    }
}

void onBinderMenu(void* user, uint16_t) {
    auto* pick = static_cast<UiMenuPick*>(user);
    if (pick == nullptr || pick->bind == nullptr) {
        return;
    }
    if (pick->bind->flags != nullptr && pick->index < pick->bind->flagN) {
        pick->bind->flags[pick->index] = pick->value;
    }
    if (pick->onFlag != nullptr) {
        pick->onFlag(pick->user, pick->index, pick->value, pick->button, pick->label);
    }
}

void onBinderCommand(void* user, uint16_t) {
    auto* pick = static_cast<UiCommandPick*>(user);
    if (pick != nullptr && pick->fn != nullptr) {
        pick->fn(pick->user, pick->arg);
    }
}

void UiPropBinder::watch(uint16_t id, uint16_t index, float fallback) {
    if (scalars != nullptr && index < scalarN) {
        scalars[index] = fallback;
    }
    rows.push_back(UiBind{.id = id, .index = index, .kind = kBindFloat, .fallback = fallback});
}

uint16_t UiPropBinder::slideStep(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV, float step) {
    if (scalars != nullptr && index < scalarN) {
        scalars[index] = def;
    }
    const uint16_t id = uiPropNumber(s, panel, label, def, minV, maxV, step);
    rows.push_back(UiBind{.id = id, .index = index, .kind = kBindFloat, .fallback = def});
    return id;
}

uint16_t UiPropBinder::slide(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV) {
    const float span = maxV - minV;
    float step = 0.01f;
    if (span > 40.0f) {
        step = 1.0f;
    } else if (span > 4.0f) {
        step = 0.1f;
    }
    return slideStep(s, panel, label, index, def, minV, maxV, step);
}

void UiPropBinder::slides(UiState& s, uint16_t panel, const UiSlideSpec* spec, uint16_t count) {
    if (spec == nullptr) {
        return;
    }
    for (uint16_t i = 0; i < count; ++i) {
        slide(s, panel, spec[i].label, spec[i].index, spec[i].def, spec[i].minV, spec[i].maxV);
    }
}

uint16_t UiPropBinder::check(UiState& s, uint16_t panel, const char* label, uint16_t index, bool def) {
    if (flags != nullptr && index < flagN) {
        flags[index] = def ? 1u : 0u;
    }
    const uint16_t id = uiPropCheck(s, panel, label, 0, 0, def);
    rows.push_back(UiBind{.id = id, .index = index, .kind = kBindFlag, .fallback = def ? 1.0f : 0.0f});
    return id;
}

uint16_t UiPropBinder::swatch(UiState& s, uint16_t panel, const char* label, uint16_t index, float r0, float g0, float b0) {
    if (scalars != nullptr && static_cast<uint32_t>(index) + 2u < scalarN) {
        scalars[index] = r0;
        scalars[static_cast<uint16_t>(index + 1u)] = g0;
        scalars[static_cast<uint16_t>(index + 2u)] = b0;
    }
    const uint16_t id = uiPropSwatch(s, panel, label, r0, g0, b0);
    rows.push_back(UiBind{.id = id, .index = index, .kind = kBindSwatch, .fallback = r0});
    return id;
}

void UiPropBinder::sync(const UiState& s) const {
    for (const UiBind& row : rows) {
        if (row.kind == kBindFloat) {
            if (scalars == nullptr || row.index >= scalarN) {
                continue;
            }
            float value = row.fallback;
            float minV = 0.0f;
            float maxV = 1.0f;
            if (uiRange(s, row.id, value, minV, maxV)) {
                scalars[row.index] = value;
            }
            continue;
        }
        if (row.kind != kBindFlag || flags == nullptr || row.index >= flagN) {
            continue;
        }
        const int on = uiCheckOn(s, row.id);
        flags[row.index] = on < 0 ? static_cast<uint8_t>(row.fallback != 0.0f) : static_cast<uint8_t>(on != 0);
    }
}

static void binderOnColor(void* user, UiState& s, uint16_t anchor, float r, float g, float b) {
    auto* binder = static_cast<UiPropBinder*>(user);
    if (binder == nullptr || anchor == kUiNone || binder->scalars == nullptr) {
        return;
    }
    for (const UiBind& row : binder->rows) {
        if (row.kind != kBindSwatch || row.id != anchor) {
            continue;
        }
        if (static_cast<uint32_t>(row.index) + 2u >= binder->scalarN) {
            continue;
        }
        binder->scalars[row.index] = r;
        binder->scalars[static_cast<uint16_t>(row.index + 1u)] = g;
        binder->scalars[static_cast<uint16_t>(row.index + 2u)] = b;
        uiPaintRgb(s, row.id, r, g, b);
    }
}

void UiPropBinder::syncSwatches(UiState& s) {
    UiColorPickerComponent* picker = uiColorFind(s);
    if (picker == nullptr) {
        return;
    }
    picker->onColor = binderOnColor;
    picker->colorUser = this;
}

void UiPropBinder::write(UiState& s, uint16_t index, float value) const {
    const uint16_t id = idOf(index, kBindFloat);
    if (id != kUiNone && id < s.count && (s.capture == id || s.focused == id)) {
        return;
    }
    if (scalars != nullptr && index < scalarN) {
        scalars[index] = value;
    }
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr || range->value == value) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    uiSetFieldText(s, id, text);
    uiNotifyVisualDirty(s);
}

uint16_t UiPropBinder::idOf(uint16_t index, uint8_t kind) const {
    for (const UiBind& row : rows) {
        if (row.kind == kind && row.index == index) {
            return row.id;
        }
    }
    return kUiNone;
}

} // namespace burnhope

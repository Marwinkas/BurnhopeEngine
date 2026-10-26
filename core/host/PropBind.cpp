#include "host/PropBind.hpp"

#include "color/Color.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/State.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

namespace {

constexpr float kLinkEps = 0.0005f;
constexpr float kHexStackH = 64.0f;
constexpr float kHexRowH = 24.0f;
constexpr float kChipH = 22.0f;
constexpr float kNumberW = 84.0f;
constexpr float kSliderMinW = 48.0f;

UiBind* findRow(std::vector<UiBind>& rows, uint16_t index, uint8_t kind) {
    for (UiBind& row : rows) {
        if (row.kind == kind && row.index == index) {
            return &row;
        }
    }
    return nullptr;
}

void swatchClick(void* user, uint16_t id) {
    auto* bind = static_cast<UiPropBinder*>(user);
    if (bind == nullptr || bind->ui == nullptr) {
        return;
    }
    UiState& s = *bind->ui;
    UiColorPickerComponent* color = uiColor(s);
    ColorBook* book = color != nullptr ? color->book : nullptr;
    if (book != nullptr && bind->scalars != nullptr) {
        for (const UiBind& row : bind->rows) {
            if (row.kind != kBindSwatch || row.id != id || static_cast<uint32_t>(row.index) + 2u >= bind->scalarN) {
                continue;
            }
            book->r = bind->scalars[row.index];
            book->g = bind->scalars[row.index + 1u];
            book->b = bind->scalars[row.index + 2u];
            book->a = 1.0f;
            book->gen += 1;
            break;
        }
    }
    colorOnOpen(&s, id);
}

void hexEdited(UiState& s, uint16_t id) {
    const UiHexBind* hex = id < s.count ? s.ent[id].try_get<UiHexBind>() : nullptr;
    const UiField* field = id < s.count ? s.ent[id].try_get<UiField>() : nullptr;
    if (hex == nullptr || hex->bind == nullptr || field == nullptr) {
        return;
    }
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
    bool hasA = false;
    if (!colorParseHex(field->bytes, r, g, b, a, hasA)) {
        return;
    }
    hex->bind->write(s, hex->rgb, r);
    hex->bind->write(s, static_cast<uint16_t>(hex->rgb + 1u), g);
    hex->bind->write(s, static_cast<uint16_t>(hex->rgb + 2u), b);
    if (hasA && hex->withAlpha != 0) {
        hex->bind->write(s, hex->alpha, a);
    }
    uiPaintRgb(s, hex->chip, r, g, b);
    if (hex->dirty != nullptr) {
        *hex->dirty = true;
    }
}

} // namespace

void UiPropBinder::attach(float* scalarBase, uint16_t scalarCount, uint8_t* flagBase, uint16_t flagCount) {
    scalars = scalarBase;
    flags = flagBase;
    scalarN = scalarCount;
    flagN = flagCount;
}

void UiPropBinder::clear() {
    rows.clear();
    vecs.clear();
    hexFields.clear();
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
    ui = &s;
    const uint16_t id = uiPropSwatch(s, panel, label, r0, g0, b0);
    uiBind(s, id, swatchClick, this);
    rows.push_back(UiBind{.id = id, .index = index, .kind = kBindSwatch, .fallback = r0});
    return id;
}

UiPropVec3 UiPropBinder::vec3(UiState& s, uint16_t host, const char* label, uint16_t index, float x, float y, float z, float minV, float maxV, float step, bool proportional) {
    const UiPropVec3 row = uiPropVec3(s, host, label, x, y, z, minV, maxV, step);
    watch(row.x, index, x);
    watch(row.y, static_cast<uint16_t>(index + 1u), y);
    watch(row.z, static_cast<uint16_t>(index + 2u), z);
    vecs.push_back(UiVecLink{.index = index, .lock = row.lock, .proportional = static_cast<uint8_t>(proportional), .last = {x, y, z}});
    return row;
}

uint16_t UiPropBinder::sliderNumber(UiState& s, uint16_t parent, uint16_t index, float def, float lo, float hi, float step) {
    const uint16_t slider = uiSlider(s, parent, def, lo, hi, nullptr, nullptr, 0.0f);
    uiSetSize(s, slider, 0.0f, -1.0f);
    uiSetGrow(s, slider, 1.0f, 1.0f, kSliderMinW);
    const uint16_t number = uiNumber(s, parent, def, lo, hi, step, nullptr, nullptr, kNumberW, kHexRowH, 0.0f);
    uiNumberFill(s, number, false);
    watch(number, index, def);
    rows.back().pair = slider;
    rows.back().last = def;
    return number;
}

uint16_t UiPropBinder::colorHex(UiState& s, uint16_t parent, uint16_t rgbIndex, uint16_t alphaIndex, bool withAlpha, bool* outDirty) {
    ui = &s;
    if (scalars != nullptr && static_cast<uint32_t>(rgbIndex) + 2u < scalarN) {
        scalars[rgbIndex] = 1.0f;
        scalars[static_cast<uint16_t>(rgbIndex + 1u)] = 1.0f;
        scalars[static_cast<uint16_t>(rgbIndex + 2u)] = 1.0f;
    }
    UiFlex col{};
    col.direction = UiFlexDir::Column;
    col.justify = UiJustify::Between;
    col.grow = 1.0f;
    col.shrink = 1.0f;
    col.heightMode = UiSize::Px;
    col.height = kHexStackH;
    col.minW = 96.0f;
    const uint16_t column = uiPanel(s, parent, col, paintClear());
    UiFlex flex{};
    flex.widthMode = UiSize::Percent;
    flex.width = 100.0f;
    flex.heightMode = UiSize::Px;
    flex.height = kChipH;
    flex.shrink = 0.0f;
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.r = 1.0f;
    paint.g = 1.0f;
    paint.b = 1.0f;
    paint.a = 1.0f;
    paint.radius = s.look.radiusSm;
    paint.borderW = 1.0f;
    const uint16_t chip = uiPanel(s, column, flex, paint);
    if (chip >= s.count) {
        return chip;
    }
    uiHover(s, chip);
    s.ent[chip].add<UiFocusable>();
    uiBind(s, chip, swatchClick, this);
    rows.push_back(UiBind{.id = chip, .index = rgbIndex, .kind = kBindSwatch, .fallback = 1.0f});
    const uint16_t field = uiField(s, column, "#FFFFFF", 0, false);
    if (field >= s.count) {
        return chip;
    }
    uiSetWidthPercent(s, field, 100.0f);
    uiSetSize(s, field, -1.0f, kHexRowH);
    uiSetGrow(s, field, -1.0f, 0.0f);
    s.ent[field].set<UiHexBind>({this, outDirty, chip, rgbIndex, alphaIndex, static_cast<uint8_t>(withAlpha)});
    uiAfterTextAdd(s, field, hexEdited);
    hexFields.push_back(field);
    return chip;
}

void UiPropBinder::sync(UiState& s) {
    for (UiBind& row : rows) {
        if (row.kind == kBindFloat) {
            if (scalars == nullptr || row.index >= scalarN) {
                continue;
            }
            float value = row.fallback;
            float minV = 0.0f;
            float maxV = 1.0f;
            if (!uiRange(s, row.id, value, minV, maxV)) {
                continue;
            }
            float slid = 0.0f;
            if (row.pair != kUiNone && uiRange(s, row.pair, slid, minV, maxV)) {
                if (slid != row.last) {
                    write(s, row.index, slid);
                    row.last = slid;
                    continue;
                }
                if (slid != value) {
                    uiRangeSet(s, row.pair, value);
                }
                row.last = value;
            }
            scalars[row.index] = value;
            continue;
        }
        if (row.kind != kBindFlag || flags == nullptr || row.index >= flagN) {
            continue;
        }
        const int on = uiCheckOn(s, row.id);
        flags[row.index] = on < 0 ? static_cast<uint8_t>(row.fallback != 0.0f) : static_cast<uint8_t>(on != 0);
    }
    if (scalars == nullptr) {
        return;
    }
    for (UiVecLink& vec : vecs) {
        if (static_cast<uint32_t>(vec.index) + 2u >= scalarN) {
            continue;
        }
        const float* cur = scalars + vec.index;
        int changed = -1;
        int count = 0;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::fabs(cur[axis] - vec.last[axis]) > kLinkEps) {
                changed = axis;
                ++count;
            }
        }
        if (count == 1 && uiPropLinked(s, vec.lock)) {
            const float oldV = vec.last[changed];
            const float newV = cur[changed];
            for (int axis = 0; axis < 3; ++axis) {
                if (axis == changed) {
                    continue;
                }
                float next = vec.last[axis];
                if (vec.proportional != 0) {
                    next = std::fabs(oldV) < 1.0e-6f ? newV : next * (newV / oldV);
                } else {
                    next += newV - oldV;
                }
                write(s, static_cast<uint16_t>(vec.index + axis), next);
            }
        }
        for (int axis = 0; axis < 3; ++axis) {
            vec.last[axis] = cur[axis];
        }
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

// Swatches show the scalars they own, so a value written from code reaches the screen without a picker edit.
void UiPropBinder::syncSwatches(UiState& s) {
    bool dirty = false;
    for (const UiBind& row : rows) {
        if (row.kind != kBindSwatch || row.id >= s.count || static_cast<uint32_t>(row.index) + 2u >= scalarN || scalars == nullptr) {
            continue;
        }
        auto* paint = s.ent[row.id].try_get_mut<UiPaint>();
        if (paint == nullptr) {
            continue;
        }
        const float r = scalars[row.index];
        const float g = scalars[row.index + 1u];
        const float b = scalars[row.index + 2u];
        if (paint->r == r && paint->g == g && paint->b == b && paint->token == UiToken::Custom) {
            continue;
        }
        paint->r = r;
        paint->g = g;
        paint->b = b;
        paint->token = UiToken::Custom;
        dirty = true;
    }
    if (dirty) {
        uiNotifyVisualDirty(s);
    }
    for (const uint16_t field : hexFields) {
        const UiHexBind* hex = field < s.count && s.focused != field ? s.ent[field].try_get<UiHexBind>() : nullptr;
        const UiField* text = hex != nullptr ? s.ent[field].try_get<UiField>() : nullptr;
        if (text == nullptr || scalars == nullptr || static_cast<uint32_t>(hex->rgb) + 2u >= scalarN || hex->alpha >= scalarN) {
            continue;
        }
        const float a = hex->withAlpha != 0 ? scalars[hex->alpha] : 1.0f;
        char next[16]{};
        colorFormatHex(next, sizeof(next), scalars[hex->rgb], scalars[hex->rgb + 1u], scalars[hex->rgb + 2u], a, hex->withAlpha != 0 && a < 0.999f);
        if (std::strcmp(text->bytes, next) != 0) {
            uiSetFieldText(s, field, next);
        }
    }
    UiColorPickerComponent* picker = uiColorFind(s);
    if (picker == nullptr) {
        return;
    }
    picker->onColor = binderOnColor;
    picker->colorUser = this;
}

void UiPropBinder::write(UiState& s, uint16_t index, float value) {
    const UiBind* row = findRow(rows, index, kBindFloat);
    const uint16_t id = row != nullptr ? row->id : kUiNone;
    if (id != kUiNone && id < s.count && (s.capture == id || s.focused == id)) {
        return;
    }
    if (scalars != nullptr && index < scalarN) {
        scalars[index] = value;
    }
    for (UiVecLink& vec : vecs) {
        if (index >= vec.index && index < vec.index + 3u) {
            vec.last[index - vec.index] = value;
        }
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
    if (row->pair != kUiNone) {
        uiRangeSet(s, row->pair, value);
    }
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

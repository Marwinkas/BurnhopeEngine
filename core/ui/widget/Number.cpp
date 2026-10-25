#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Number.hpp"

#include <cstdio>

namespace burnhope {

void uiWriteNumber(char* dst, std::size_t cap, float value) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    std::snprintf(dst, cap, "%.4g", static_cast<double>(value));
}

bool uiRange(const UiState& s, uint16_t id, float& value, float& minV, float& maxV) {
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    value = range->value;
    minV = range->min;
    maxV = range->max;
    return true;
}

void dragSpin(UiState& s, uint16_t id, float x) {
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr) {
        return;
    }
    const float delta = x - s.grabX;
    if (delta > -3.0f && delta < 3.0f) {
        return;
    }
    const float step = range->step > 0.0f ? range->step : 0.01f;
    range->value += (delta / 8.0f) * step;
    if (range->value < range->min) {
        range->value = range->min;
    }
    if (range->value > range->max) {
        range->value = range->max;
    }
    s.grabX = x;
    char text[32]{};
    uiWriteNumber(text, sizeof(text), range->value);
    uiText(s, id, text);
    if (const UiValueHook* hook = s.ent[id].try_get<UiValueHook>(); hook != nullptr && hook->fn != nullptr) {
        hook->fn(hook->user, range->value);
    }
    s.visualDirty = true;
}

void uiNumberFill(UiState& s, uint16_t id, bool showFillProgress) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    range->fill = showFillProgress ? 1 : 0;
}

uint16_t uiSpin(UiState& s, uint16_t parent, float value, float minV, float maxV, float step) {
    UiFlex flex{};
    flex.heightMode = UiSize::Px;
    flex.height = 30.0f;
    flex.shrink = 0.0f;
    flex.align = UiAlign::Center;
    UiPaint body = paintToken(s.look, UiToken::Button, s.look.radiusSm);
    body.shadow = 4.0f;
    body.borderW = 2.0f;
    body.br = s.look.text[0];
    body.bg = s.look.text[1];
    body.bb = s.look.text[2];
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, step > 0.0f ? step : 0.01f});
    char text[32]{};
    uiWriteNumber(text, sizeof(text), value);
    uiText(s, id, text);
    return id;
}

namespace {

bool drawNumberFill(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr || range->step <= 0.0f) {
        return false;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr || range->fill == 0) {
        return true;
    }
    if (range->max <= range->min + 0.0001f || range->max - range->min >= 1.0e8f) {
        return true;
    }
    float span = (range->value - range->min) / (range->max - range->min);
    if (span < 0.0f) {
        span = 0.0f;
    }
    if (span > 1.0f) {
        span = 1.0f;
    }
    if (span <= 0.001f) {
        return true;
    }
    const UiBox& box = s.box[id];
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    n = emitRect(dst, n, cap, box.x + 1.0f, box.y + 1.0f, (box.w - 2.0f) * span, box.h - 2.0f,
        s.look.muted[0], s.look.muted[1], s.look.muted[2], 0.85f, paint->radius, clip);
    return true;
}

} // namespace

void uiNumberBind(UiState& s) {
    uiDrawUnderAdd(s, drawNumberFill);
}

} // namespace burnhope

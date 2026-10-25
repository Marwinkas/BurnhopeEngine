#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/State.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {

void dragSlider(UiState& s, uint16_t id, float x) {
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range == nullptr) {
        return;
    }
    const UiBox& b = s.box[id];
    float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    range->value = range->min + t * (range->max - range->min);
    if (const UiValueHook* hook = s.ent[id].try_get<UiValueHook>(); hook != nullptr && hook->fn != nullptr) {
        hook->fn(hook->user, range->value);
    }
    char note[32];
    std::snprintf(note, sizeof(note), "slider %.3f", range->value);
    uiTrace(s, note, id);
    s.visualDirty = true;
}

uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange, void* user, float width) {
    UiFlex flex{};
    if (width > 1.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = width;
        flex.grow = 0.0f;
    } else {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
    }
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.sliderH;
    flex.shrink = 0.0f;
    UiPaint body = paintToken(s.look, UiToken::Slider, s.look.radiusSm);
    body.a = 0.0f;
    body.borderW = 0.0f;
    body.shadow = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, body);
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    if (value < minV) {
        value = minV;
    }
    if (value > maxV) {
        value = maxV;
    }
    s.ent[id].set<UiRange>({value, minV, maxV, 0.0f});
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
    return id;
}

void uiBindSlider(UiState& s, const char* name, float value, UiValueChangeFn onChange, void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    if (value < range->min) {
        value = range->min;
    }
    if (value > range->max) {
        value = range->max;
    }
    range->value = value;
    if (onChange != nullptr) {
        s.ent[id].set<UiValueHook>({onChange, user});
    }
}

void uiSliderSet(UiState& s, uint16_t id, float value) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    if (value < range->min) {
        value = range->min;
    }
    if (value > range->max) {
        value = range->max;
    }
    range->value = value;
}

bool uiSliderInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note) {
    (void)note;
    if (id >= s.count) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    if (range->step > 0.0f) {
        return false;
    }
    if (phase == kUiInPressLate) {
        s.capture = id;
        dragSlider(s, id, in.pointer.x);
        return true;
    }
    if (phase == kUiInDrag) {
        dragSlider(s, id, in.pointer.x);
        return true;
    }
    return false;
}

namespace {

uint32_t paintTrack(const UiState& s, uint16_t id, const UiRange& range, UiPrimitive* dst, uint32_t n, uint32_t cap) {
    const UiBox& box = s.box[id];
    if (box.w < 8.0f || box.h < 8.0f) {
        return n;
    }
    UiBox ancestor{};
    const bool hasClip = uiClipBox(s, id, ancestor);
    const UiBox* clip = hasClip ? &ancestor : nullptr;
    const float span = range.max - range.min;
    float t = span > 0.0001f ? (range.value - range.min) / span : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    const float inset = 2.0f;
    const float trackH = 4.0f;
    const float trackW = box.w - inset * 2.0f;
    const float trackY = box.y + (box.h - trackH) * 0.5f;
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW, trackH,
        s.look.muted[0], s.look.muted[1], s.look.muted[2], 0.7f, trackH * 0.5f, clip);
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW * t, trackH,
        s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2], 1.0f, trackH * 0.5f, clip);
    const float thumb = box.h > 16.0f ? 14.0f : box.h - 4.0f;
    const float travel = trackW > thumb ? trackW - thumb : 0.0f;
    const float tx = box.x + inset + travel * t;
    const float ty = box.y + (box.h - thumb) * 0.5f;
    return emitRect(dst, n, cap, tx, ty, thumb, thumb,
        s.look.text[0], s.look.text[1], s.look.text[2], 1.0f, thumb * 0.5f, clip);
}

bool drawSlider(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (s.ent[id].has<UiProgress>()) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr || range->step > 0.0f) {
        return false;
    }
    n = paintTrack(s, id, *range, dst, n, cap);
    return true;
}

bool drawProgress(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    if (!s.ent[id].has<UiProgress>()) {
        return false;
    }
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (range == nullptr) {
        return false;
    }
    n = paintTrack(s, id, *range, dst, n, cap);
    return true;
}

void adornSlider(UiState& s, uint16_t id) {
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void finishSlider(UiState& s, uint16_t id, const BinTail* spec, const char*, const char*) {
    float value = 0.0f;
    float minV = 0.0f;
    float maxV = 0.0f;
    if (spec != nullptr) {
        std::memcpy(&minV, spec->raw + 10, sizeof(minV));
        std::memcpy(&maxV, spec->raw + 14, sizeof(maxV));
        std::memcpy(&value, spec->raw + 18, sizeof(value));
    }
    s.ent[id].set<UiRange>({value, minV, maxV});
}

void finishProgress(UiState& s, uint16_t id, const BinTail* spec, const char*, const char*) {
    finishSlider(s, id, spec, nullptr, nullptr);
    s.ent[id].add<UiProgress>();
}

} // namespace

void uiSliderBin(UiState& s) {
    uiBinRole(s, kBinSlotSlider, "slider", static_cast<uint8_t>(UiToken::Slider), kBinRoleGhost);
    uiBinRole(s, kBinSlotProgress, "progress", 0, 0);
    uiRegisterSpawnHook(s, kBinSlotSlider, adornSlider);
    uiRegisterRoleFinish(s, kBinSlotSlider, finishSlider);
    uiRegisterRoleFinish(s, kBinSlotProgress, finishProgress);
    uiDrawDecorAdd(s, drawSlider);
    uiDrawDecorAdd(s, drawProgress);
}

} // namespace burnhope

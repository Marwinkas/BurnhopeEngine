#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"

#include <algorithm>
#include "ui/Template.hpp"

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

uint16_t uiSlider(UiState& s, uint16_t parent, float value, float minV, float maxV, UiValueChangeFn onChange, void* user) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/slider");
    uiSliderInit(s, id, value, minV, maxV, onChange, user);
    return id;
}

void uiSliderInit(UiState& s, uint16_t id, float value, float minV, float maxV, UiValueChangeFn onChange, void* user) {
    if (id >= s.count) {
        return;
    }
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
        uiCapture(s, id);
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
    const float minDraw = uiStyle(s, "slider", UiStyleKey::MinDraw);
    if (box.w < minDraw || box.h < minDraw) {
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
    using K = UiStyleKey;
    const float inset = uiStyle(s, "slider", K::TrackInset);
    const float trackH = uiStyle(s, "slider", K::TrackH);
    const UiRgb trackInk = uiStyleTint(s, "slider", UiStyleTone::Track);
    const UiRgb fillInk = uiStyleTint(s, "slider", UiStyleTone::Fill);
    const UiRgb thumbInk = uiStyleTint(s, "slider", UiStyleTone::Thumb);
    const float trackW = box.w - inset * 2.0f;
    const float trackY = box.y + (box.h - trackH) * 0.5f;
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW, trackH,
        trackInk.r, trackInk.g, trackInk.b, uiStyle(s, "slider", K::TrackAlpha), trackH * 0.5f, clip);
    n = emitRect(dst, n, cap, box.x + inset, trackY, trackW * t, trackH,
        fillInk.r, fillInk.g, fillInk.b, 1.0f, trackH * 0.5f, clip);
    const float thumb = std::min(uiStyle(s, "slider", K::ThumbSide), box.h - uiStyle(s, "slider", K::ThumbPad));
    const float travel = trackW > thumb ? trackW - thumb : 0.0f;
    const float tx = box.x + inset + travel * t;
    const float ty = box.y + (box.h - thumb) * 0.5f;
    return emitRect(dst, n, cap, tx, ty, thumb, thumb,
        thumbInk.r, thumbInk.g, thumbInk.b, 1.0f, thumb * 0.5f, clip);
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
    uiSetFocusable(s, id);
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

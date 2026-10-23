#pragma once

#include "ui/widget/List.hpp"

namespace burnhope {

struct UiFloatBind {
    uint16_t id = kUiNone;
    float* dst = nullptr;
    float fallback = 0.0f;
};

struct UiBoolBind {
    uint16_t id = kUiNone;
    uint8_t* dst = nullptr;
    uint8_t fallback = 0;
};

struct UiBitBind {
    uint16_t id = kUiNone;
    bool* dst = nullptr;
    uint8_t fallback = 0;
};

struct UiSwatchBind {
    uint16_t id = kUiNone;
    float* r = nullptr;
    float* g = nullptr;
    float* b = nullptr;
};

struct UiSlideSpec {
    const char* label = nullptr;
    float* dst = nullptr;
    float def = 0.0f;
    float minV = 0.0f;
    float maxV = 1.0f;
};

// One record per control, filled when the row is created. The frame copies the
// live widget into *dst. No name and no second lookup.
struct UiPropBinder {
    static constexpr uint16_t kFloats = 384;
    static constexpr uint16_t kBools = 96;
    static constexpr uint16_t kSwatches = 8;

    UiFloatBind floats[kFloats]{};
    UiBoolBind bools[kBools]{};
    UiBitBind bits[kBools]{};
    UiSwatchBind swatches[kSwatches]{};
    uint16_t nFloats = 0;
    uint16_t nBools = 0;
    uint16_t nBits = 0;
    uint16_t nSwatches = 0;

    void clear();
    uint16_t slide(UiState& s, uint16_t panel, const char* label, float* dst, float def, float minV, float maxV);
    void watch(uint16_t id, float* dst, float fallback);
    uint16_t slideStep(UiState& s, uint16_t panel, const char* label, float* dst, float def, float minV, float maxV, float step);
    void slides(UiState& s, uint16_t panel, const UiSlideSpec* spec, uint16_t count);
    uint16_t check(UiState& s, uint16_t panel, const char* label, uint8_t* dst, bool def);
    uint16_t check(UiState& s, uint16_t panel, const char* label, bool* dst, bool def);
    uint16_t swatch(UiState& s, uint16_t panel, const char* label, float* r, float* g, float* b, float r0, float g0, float b0);
    void sync(const UiState& s) const;
    void syncSwatches(UiState& s) const;
    void write(UiState& s, float* dst, float value) const;
    [[nodiscard]] uint16_t idOf(const float* dst) const;
    [[nodiscard]] uint16_t idOf(const uint8_t* dst) const;
};

} // namespace burnhope

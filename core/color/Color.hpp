#pragma once

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace burnhope {

struct ColorRgba8 {
    uint32_t rgba = 0;
    uint32_t reserved[3]{};
};

struct ColorLinear {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

static_assert(std::is_trivially_copyable_v<ColorRgba8>);
static_assert(sizeof(ColorRgba8) == 16);
static_assert(std::is_trivially_copyable_v<ColorLinear>);
static_assert(sizeof(ColorLinear) == 16);

constexpr uint32_t colorPackBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(a) << 24);
}

inline uint8_t colorByte(uint32_t packed, uint32_t shift) {
    return static_cast<uint8_t>((packed >> shift) & 0xFFu);
}

inline float colorSelect(float lo, float hi, float takeHi) {
    return lo + (hi - lo) * takeHi;
}

inline float colorSrgbToLinear(float encoded) {
    const float lo = encoded / 12.92f;
    const float hi = std::pow((encoded + 0.055f) / 1.055f, 2.4f);
    const float takeHi = encoded > 0.04045f ? 1.0f : 0.0f;
    return colorSelect(lo, hi, takeHi);
}

inline float colorLinearToSrgb(float linear) {
    const float lo = linear * 12.92f;
    const float hi = 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    const float takeHi = linear > 0.0031308f ? 1.0f : 0.0f;
    return colorSelect(lo, hi, takeHi);
}

inline ColorLinear colorSrgb8ToLinear(ColorRgba8 color) {
    ColorLinear out{};
    out.r = colorSrgbToLinear(static_cast<float>(colorByte(color.rgba, 0)) / 255.0f);
    out.g = colorSrgbToLinear(static_cast<float>(colorByte(color.rgba, 8)) / 255.0f);
    out.b = colorSrgbToLinear(static_cast<float>(colorByte(color.rgba, 16)) / 255.0f);
    out.a = static_cast<float>(colorByte(color.rgba, 24)) / 255.0f;
    return out;
}

inline uint8_t colorQuantize(float encoded) {
    float scaled = encoded * 255.0f + 0.5f;
    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    if (scaled > 255.0f) {
        scaled = 255.0f;
    }
    return static_cast<uint8_t>(scaled);
}

inline ColorRgba8 colorLinearToSrgb8(ColorLinear color) {
    ColorRgba8 out{};
    out.rgba = colorPackBytes(colorQuantize(colorLinearToSrgb(color.r)), colorQuantize(colorLinearToSrgb(color.g)), colorQuantize(colorLinearToSrgb(color.b)), colorQuantize(color.a));
    return out;
}

inline void colorLinearToHsv(float r, float g, float b, float& h, float& s, float& v) {
    const float maxc = r > g ? (r > b ? r : b) : (g > b ? g : b);
    const float minc = r < g ? (r < b ? r : b) : (g < b ? g : b);
    v = maxc;
    const float d = maxc - minc;
    s = maxc <= 0.0001f ? 0.0f : d / maxc;
    if (d <= 0.0001f) {
        h = 0.0f;
        return;
    }
    if (maxc == r) {
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    } else if (maxc == g) {
        h = (b - r) / d + 2.0f;
    } else {
        h = (r - g) / d + 4.0f;
    }
    h /= 6.0f;
}

inline void colorHsvToLinear(float h, float s, float v, ColorLinear& out) {
    float hh = h * 6.0f;
    if (hh >= 6.0f) {
        hh = 0.0f;
    }
    const int sector = static_cast<int>(hh);
    const float f = hh - static_cast<float>(sector);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    const float table[6][3] = {
        {v, t, p},
        {q, v, p},
        {p, v, t},
        {p, q, v},
        {t, p, v},
        {v, p, q},
    };
    const int index = sector < 0 ? 0 : (sector > 5 ? 5 : sector);
    out.r = table[index][0];
    out.g = table[index][1];
    out.b = table[index][2];
    out.a = 1.0f;
}

} // namespace burnhope

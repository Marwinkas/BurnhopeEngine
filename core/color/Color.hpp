#pragma once

#include "Str.hpp"

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
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

// Parses "#RRGGBB", "#RRGGBBAA" (hash optional) into 0..1 channels; `a` is written only when hasA.
inline bool colorParseHex(const char* text, float& r, float& g, float& b, float& a, bool& hasA) {
    if (text == nullptr) {
        return false;
    }
    if (*text == '#') {
        ++text;
    }
    size_t n = 0;
    while (text[n] != '\0' && n < 8) {
        if (uiHexNib(text[n]) < 0) {
            return false;
        }
        ++n;
    }
    if (text[n] != '\0' || (n != 6 && n != 8)) {
        return false;
    }
    uint32_t packed = 0;
    const auto parsed = std::from_chars(text, text + n, packed, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != text + n) {
        return false;
    }
    hasA = n == 8;
    const uint32_t rgb = hasA ? packed >> 8 : packed;
    r = static_cast<float>((rgb >> 16) & 255u) / 255.0f;
    g = static_cast<float>((rgb >> 8) & 255u) / 255.0f;
    b = static_cast<float>(rgb & 255u) / 255.0f;
    if (hasA) {
        a = static_cast<float>(packed & 255u) / 255.0f;
    }
    return true;
}

// Writes "#RRGGBB" or "#RRGGBBAA" (uppercase) and a terminator; truncates to cap. Returns the length written.
inline size_t colorFormatHex(char* dst, size_t cap, float r, float g, float b, float a, bool withA) {
    if (dst == nullptr || cap == 0) {
        return 0;
    }
    static constexpr char kDigits[] = "0123456789ABCDEF";
    const uint8_t bytes[4] = {colorQuantize(r), colorQuantize(g), colorQuantize(b), colorQuantize(a)};
    char full[10]{};
    full[0] = '#';
    const size_t count = withA ? 4 : 3;
    for (size_t i = 0; i < count; ++i) {
        full[1 + i * 2] = kDigits[bytes[i] >> 4];
        full[2 + i * 2] = kDigits[bytes[i] & 15u];
    }
    return strCopyCap(dst, cap, full);
}

// Rec.709 luminance of a linear colour.
constexpr float colorLuma(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// Blackbody tint (Tanner Helland fit) for 1500..12000 K, each channel in 0..1.
inline void colorFromKelvin(float kelvin, float& r, float& g, float& b) {
    const float t = std::fmin(std::fmax(kelvin, 1500.0f), 12000.0f) / 100.0f;
    auto sat = [](float v) { return std::fmin(std::fmax(v, 0.0f), 255.0f); };
    if (t <= 66.0f) {
        r = 255.0f;
        g = sat(99.4708025861f * std::log(t) - 161.1195681661f);
        b = t <= 19.0f ? 0.0f : sat(138.5177312231f * std::log(t - 10.0f) - 305.0447927307f);
    } else {
        r = sat(329.698727446f * std::pow(t - 60.0f, -0.1332047592f));
        g = sat(288.1221695283f * std::pow(t - 60.0f, -0.0755148492f));
        b = 255.0f;
    }
    r /= 255.0f;
    g /= 255.0f;
    b /= 255.0f;
}

// IEEE binary16 bits, truncating; denormals flush to zero and overflow saturates to infinity.
inline uint16_t floatToHalf(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16u) & 0x8000u;
    const int32_t exp = static_cast<int32_t>((bits >> 23u) & 0xffu) - 127 + 15;
    const uint32_t mant = bits & 0x7fffffu;
    if (exp <= 0) {
        return static_cast<uint16_t>(sign);
    }
    if (exp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00u);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10u) | (mant >> 13u));
}

} // namespace burnhope

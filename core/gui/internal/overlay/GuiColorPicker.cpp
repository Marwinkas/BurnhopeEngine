#include "overlay/GuiColorPicker.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope::gui {
namespace {

constexpr const char* kChannelKey[3] = {"colorpick.r", "colorpick.g", "colorpick.b"};
constexpr const char* kHexKey = "colorpick.hex";
constexpr const char* kSwatchKey = "colorpick.swatch";
constexpr const char* kPopupName = "colorPicker";

void formatHex(char (&out)[16], const float (&rgb)[3]) {
    unsigned v[3];
    for (int i = 0; i < 3; ++i) {
        v[i] = static_cast<unsigned>(std::lround(std::clamp(rgb[i], 0.0f, 1.0f) * 255.0f));
    }
    std::snprintf(out, sizeof(out), "#%02X%02X%02X", v[0], v[1], v[2]);
}

bool parseHex(const char* text, float (&rgb)[3]) {
    if (text[0] == '#') {
        ++text;
    }
    if (std::strlen(text) != 6) {
        return false;
    }
    float out[3];
    for (int i = 0; i < 3; ++i) {
        unsigned v = 0;
        const auto r = std::from_chars(text + i * 2, text + i * 2 + 2, v, 16);
        if (r.ec != std::errc{} || r.ptr != text + i * 2 + 2) {
            return false;
        }
        out[i] = static_cast<float>(v) / 255.0f;
    }
    std::copy_n(out, 3, rgb);
    return true;
}

// Mirrors the colour into the swatch, the hex field and (unless `sliders` is false) the three sliders.
void show(GuiColorPicker& p, bool sliders) {
    if (sliders) {
        for (uint32_t i = 0; i < 3; ++i) {
            guiHostSetValue(p.host, kChannelKey[i], p.rgb[i]);
        }
    }
    char hex[16];
    formatHex(hex, p.rgb);
    guiHostSetText(p.host, kHexKey, hex);
    guiHostSetColor(p.host, kSwatchKey, p.rgb[0], p.rgb[1], p.rgb[2]);
}

void emit(GuiColorPicker& p) {
    if (p.fn != nullptr) {
        p.fn(p.user, p.rgb[0], p.rgb[1], p.rgb[2]);
    }
}

void onChannel(void* user, float value) {
    auto* c = static_cast<GuiColorPicker::Channel*>(user);
    GuiColorPicker& p = *c->owner;
    if (p.rgb[c->index] != value) {
        p.rgb[c->index] = value;
        show(p, false);
        emit(p);
    }
}

void onHex(void* user, const char* text) {
    auto* p = static_cast<GuiColorPicker*>(user);
    if (parseHex(text, p->rgb)) {
        show(*p, true);
        emit(*p);
    }
}

} // namespace

bool guiColorPickerOpen(GuiHost* host, GuiColorPicker& picker, const char* anchor, const float (&rgb)[3], GuiColorFn fn, void* user) {
    picker.host = host;
    if (!picker.bound) {
        bool ok = true;
        for (uint32_t i = 0; i < 3; ++i) {
            picker.channel[i] = {&picker, i};
            ok = guiHostBindDrag(host, kChannelKey[i], onChannel, &picker.channel[i]) && ok;
        }
        ok = guiHostBindEdit(host, kHexKey, onHex, &picker) && ok;
        picker.bound = true; // one attempt only: a missing popup must not eat a bind slot per click
        if (!ok) {
            return false;
        }
    }
    picker.fn = nullptr; // loading the values must not look like an edit
    std::copy_n(rgb, 3, picker.rgb);
    show(picker, true);
    picker.fn = fn;
    picker.user = user;
    guiHostOpenAt(host, kPopupName, anchor);
    return true;
}

} // namespace burnhope::gui

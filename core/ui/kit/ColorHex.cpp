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

#include <algorithm>
#include <charconv>
#include <cstring>

namespace burnhope {

bool readHex(const UiField& field, float& r, float& g, float& b, float& a) {
    char text[16]{};
    std::memcpy(text, field.bytes, std::min<size_t>(field.len, sizeof(text) - 1));
    bool hasA = false;
    return colorParseHex(text, r, g, b, a, hasA);
}

bool readInt(const UiField& field, int& out) {
    unsigned value = 0;
    const char* end = field.bytes + field.len;
    const auto parsed = std::from_chars(field.bytes, end, value);
    if (field.len == 0 || parsed.ec != std::errc{} || parsed.ptr != end || value > 0x7fffffffu) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

void colorOnValue(void* user, uint16_t id, float val) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    UiColorPickerComponent& c = (*picker);
    bool fromHsv = false;
    if (id == c.colorRgb[0]) {
        colorInk(c).r = val;
    } else if (id == c.colorRgb[1]) {
        colorInk(c).g = val;
    } else if (id == c.colorRgb[2]) {
        colorInk(c).b = val;
    } else if (id == c.colorAlpha) {
        colorInk(c).a = val;
    } else if (id == c.colorHsv[0]) {
        colorInk(c).h = val;
        fromHsv = true;
    } else if (id == c.colorHsv[1]) {
        colorInk(c).s = val;
        fromHsv = true;
    } else if (id == c.colorHsv[2]) {
        colorInk(c).v = val;
        fromHsv = true;
    } else {
        return;
    }
    applyColor(s, *picker, fromHsv);
}

void slideChannel(void* user, int channel, float value) {
    auto* state = static_cast<UiState*>(user);
    if (state == nullptr) {
        return;
    }
    const uint16_t live = state->capture != kUiNone ? state->capture : state->focused;
    const UiColorPickerComponent* color = pickerFor(*state, live);
    if (color == nullptr) {
        return;
    }
    uint16_t id = kUiNone;
    if (channel < 3) {
        id = color->colorRgb[channel];
    } else if (channel == 3) {
        id = color->colorAlpha;
    } else if (channel < 7) {
        id = color->colorHsv[channel - 4];
    }
    colorOnValue(user, id, value);
}

void slideR(void* user, float value) { slideChannel(user, 0, value); }
void slideG(void* user, float value) { slideChannel(user, 1, value); }
void slideB(void* user, float value) { slideChannel(user, 2, value); }
void slideA(void* user, float value) { slideChannel(user, 3, value); }
void slideH(void* user, float value) { slideChannel(user, 4, value); }
void slideS(void* user, float value) { slideChannel(user, 5, value); }
void slideV(void* user, float value) { slideChannel(user, 6, value); }

void colorOnDrag(void* user, uint16_t id, float x, float y) {
    auto& s = *static_cast<UiState*>(user);
    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    dragWheel(s, *picker, id, x, y);
}

void colorOnEdit(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    const UiField* field = s.ent[id].try_get<UiField>();
    if (field == nullptr) {
        return;
    }
    if (id == (*picker).colorHex) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = colorInk((*picker)).a;
        if (readHex(*field, r, g, b, a)) {
            colorInk((*picker)).r = r;
            colorInk((*picker)).g = g;
            colorInk((*picker)).b = b;
            colorInk((*picker)).a = a;
            applyColor(s, *picker, false);
        }
        return;
    }
    int which = -1;
    for (int i = 0; i < 7; ++i) {
        if ((*picker).colorNum[i] == id) {
            which = i;
        }
    }
    if (which < 0) {
        return;
    }
    int n = 0;
    if (!readInt(*field, n)) {
        return;
    }
    if (which <= 3) {
        if (n > 255) {
            n = 255;
        }
        const float u = static_cast<float>(n) / 255.0f;
        if (which == 0) {
            colorInk((*picker)).r = u;
        } else if (which == 1) {
            colorInk((*picker)).g = u;
        } else if (which == 2) {
            colorInk((*picker)).b = u;
        } else {
            colorInk((*picker)).a = u;
        }
        applyColor(s, *picker, false);
    } else if (which == 4) {
        if (n > 360) {
            n = 360;
        }
        colorInk((*picker)).h = static_cast<float>(n) / 360.0f;
        applyColor(s, *picker, true);
    } else {
        if (n > 100) {
            n = 100;
        }
        const float u = static_cast<float>(n) / 100.0f;
        if (which == 5) {
            colorInk((*picker)).s = u;
        } else {
            colorInk((*picker)).v = u;
        }
        applyColor(s, *picker, true);
    }
}

bool colorGrab(void* user, uint16_t hit, float x, float y) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, hit);
    if (picker == nullptr) {
        return false;
    }
    const bool dropping = colorInk((*picker)).drop != 0 || ((*picker).book != nullptr && (*picker).book->drop != 0);
    if (!dropping) {
        return false;
    }
    if (hit == (*picker).colorPickBtn) {
        return false;
    }
    float sr = 0.0f;
    float sg = 0.0f;
    float sb = 0.0f;
    float sa = 1.0f;
    samplePixel(s, x, y, sr, sg, sb, sa);
    colorInk((*picker)).r = sr;
    colorInk((*picker)).g = sg;
    colorInk((*picker)).b = sb;
    colorInk((*picker)).a = sa;
    colorInk((*picker)).drop = 0;
    applyColor(s, *picker, false);
    if ((*picker).colorHost == 0 && colorInk((*picker)).present == 1) {
        placeColorMenu(s, *picker);
        uiShow(s, (*picker).colorPanel, true);
    }
    return true;
}

} // namespace burnhope

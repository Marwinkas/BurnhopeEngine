#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/Field.hpp"
#include "ui/Layout.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "color/Color.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {
namespace {
constexpr uint8_t kRampColorWheel = 10;
constexpr uint8_t kRampColorValue = 11;
constexpr uint8_t kRampColorSwatch = 12;
constexpr uint8_t kRampColorAlpha = 13;

UiPaint colorRampPart(const UiLook& look, UiRole role, UiToken token, uint8_t ramp, float radius = 0.0f) {
    UiPaint paint = paintToken(look, role, token, radius);
    paint.ramp = ramp;
    return paint;
}
} // namespace

void publishColor(UiState& s);
void refreshColorChrome(UiState& s);

void uiSetColorBook(UiState& s, ColorBook* book) {
    uiColor(s).book = book;
}

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    ColorLinear color{};
    colorHsvToLinear(h, s, v, color);
    r = color.r;
    g = color.g;
    b = color.b;
}

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
    colorLinearToHsv(r, g, b, h, s, v);
}

void setRange(UiState& s, uint16_t id, float value) {
    if (id == kUiNone) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range != nullptr) {
        range->value = value;
    }
}

void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a, bool withAlpha) {
    auto nib = [](int v) { return static_cast<char>(v < 10 ? '0' + v : 'A' + (v - 10)); };
    auto byte = [&](float c, int at) {
        int v = static_cast<int>(c * 255.0f + 0.5f);
        if (v < 0) {
            v = 0;
        }
        if (v > 255) {
            v = 255;
        }
        dst[at] = nib(v >> 4);
        dst[at + 1] = nib(v & 15);
    };
    dst[0] = '#';
    byte(r, 1);
    byte(g, 3);
    byte(b, 5);
    if (withAlpha) {
        byte(a, 7);
        dst[9] = '\0';
        len = 9;
    } else {
        dst[7] = '\0';
        len = 7;
    }
}

void applyColor(UiState& s, bool fromHsv, bool publish = true) {
    if (fromHsv) {
        hsvToRgb(uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV, uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB);
    } else {
        rgbToHsv(uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV);
    }
    setRange(s, uiColor(s).colorRgb[0], uiColor(s).colorR);
    setRange(s, uiColor(s).colorRgb[1], uiColor(s).colorG);
    setRange(s, uiColor(s).colorRgb[2], uiColor(s).colorB);
    setRange(s, uiColor(s).colorHsv[0], uiColor(s).colorH);
    setRange(s, uiColor(s).colorHsv[1], uiColor(s).colorS);
    setRange(s, uiColor(s).colorHsv[2], uiColor(s).colorV);
    if (uiColor(s).colorSwatch != kUiNone) {
        auto* paint = s.ent[uiColor(s).colorSwatch].try_get_mut<UiPaint>();
        if (paint != nullptr) {
            paint->r = uiColor(s).colorR;
            paint->g = uiColor(s).colorG;
            paint->b = uiColor(s).colorB;
            paint->a = uiColor(s).colorA;
        }
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* ramp = s.ent[id].try_get_mut<UiPaint>();
        if (ramp != nullptr && ramp->ramp >= 5 && ramp->ramp <= 7) {
            ramp->r = uiColor(s).colorR;
            ramp->g = uiColor(s).colorG;
            ramp->b = uiColor(s).colorB;
        }
    }
    auto write = [&](uint16_t id, bool keepCaret) {
        if (id == kUiNone) {
            return;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr) {
            return;
        }
        if (keepCaret && s.focused == id) {
            return;
        }
        writeHex(field->bytes, field->len, uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorA, true);
        field->caret = field->len;
        field->anchor = field->len;
        fieldSyncText(s, id);
    };
    write(uiColor(s).colorTarget, false);
    write(uiColor(s).colorHex, true);
    const int shown[7] = {
        static_cast<int>(uiColor(s).colorR * 255.0f + 0.5f),
        static_cast<int>(uiColor(s).colorG * 255.0f + 0.5f),
        static_cast<int>(uiColor(s).colorB * 255.0f + 0.5f),
        static_cast<int>(uiColor(s).colorA * 255.0f + 0.5f),
        static_cast<int>(uiColor(s).colorH * 360.0f + 0.5f),
        static_cast<int>(uiColor(s).colorS * 100.0f + 0.5f),
        static_cast<int>(uiColor(s).colorV * 100.0f + 0.5f),
    };
    for (int i = 0; i < 7; ++i) {
        const uint16_t id = uiColor(s).colorNum[i];
        if (id == kUiNone) {
            continue;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr || s.focused == id) {
            continue;
        }
        uiWriteInt(field->bytes, field->len, shown[i]);
        field->caret = field->len;
        field->anchor = field->len;
        fieldSyncText(s, id);
    }
    setRange(s, uiColor(s).colorAlpha, uiColor(s).colorA);
    if (publish) {
        publishColor(s);
    }
    refreshColorChrome(s);
    s.visualDirty = true;
}

void takeBook(UiState& s) {
    if (uiColor(s).book == nullptr || uiColor(s).book->gen == 0) {
        return;
    }
    uiColor(s).colorR = uiColor(s).book->r;
    uiColor(s).colorG = uiColor(s).book->g;
    uiColor(s).colorB = uiColor(s).book->b;
    uiColor(s).colorA = uiColor(s).book->a;
    uiColor(s).colorH = uiColor(s).book->h;
    uiColor(s).colorS = uiColor(s).book->s;
    uiColor(s).colorV = uiColor(s).book->v;
    uiColor(s).colorUseAlpha = uiColor(s).book->useAlpha;
    uiColor(s).colorMode = uiColor(s).book->mode;
    uiColor(s).colorPresent = uiColor(s).book->present;
    uiColor(s).colorDrop = uiColor(s).book->drop;
    uiColor(s).colorSeen = uiColor(s).book->gen;
    uiColor(s).colorApplied = uiColor(s).book->present;
}

void paintColor(UiState& s) {
    applyColor(s, false);
}

void dragWheel(UiState& s, uint16_t id, float x, float y) {
    const UiBox& b = s.box[id];
    if (id == uiColor(s).colorWheel) {
        const float cx = b.x + b.w * 0.5f;
        const float cy = b.y + b.h * 0.5f;
        const float dx = x - cx;
        const float dy = y - cy;
        const float radius = std::min(b.w, b.h) * 0.5f;
        float sat = radius > 1.0f ? std::sqrt(dx * dx + dy * dy) / radius : 0.0f;
        if (sat > 1.0f) {
            sat = 1.0f;
        }
        float hue = std::atan2(dy, dx) * 0.15915494f + 0.5f;
        if (hue < 0.0f) {
            hue += 1.0f;
        }
        if (hue >= 1.0f) {
            hue -= 1.0f;
        }
        uiColor(s).colorH = hue;
        uiColor(s).colorS = sat;
        applyColor(s, true);
    } else if (id == uiColor(s).colorVal) {
        float t = b.h > 1.0f ? 1.0f - (y - b.y) / b.h : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        uiColor(s).colorV = t;
        applyColor(s, true);
    } else if (id == uiColor(s).colorAlpha) {
        float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        uiColor(s).colorA = t;
        applyColor(s, true);
    }
}

bool readHex(const UiField& field, float& r, float& g, float& b, float& a) {
    uint8_t i = 0;
    if (field.len > 0 && field.bytes[0] == '#') {
        i = 1;
    }
    char digits[8]{};
    int n = 0;
    for (; i < field.len && n < 8; ++i) {
        if (uiHexNib(field.bytes[i]) < 0) {
            return false;
        }
        digits[n++] = field.bytes[i];
    }
    if (n != 6 && n != 8) {
        return false;
    }
    auto chan = [&](int at) {
        return (uiHexNib(digits[at]) * 16 + uiHexNib(digits[at + 1])) / 255.0f;
    };
    r = chan(0);
    g = chan(2);
    b = chan(4);
    if (n == 8) {
        a = chan(6);
    }
    return true;
}

bool readInt(const UiField& field, int& out) {
    if (field.len == 0) {
        return false;
    }
    int v = 0;
    for (uint8_t i = 0; i < field.len; ++i) {
        const char ch = field.bytes[i];
        if (ch < '0' || ch > '9') {
            return false;
        }
        v = v * 10 + (ch - '0');
    }
    out = v;
    return true;
}

void samplePixel(const UiState& s, float x, float y, float& r, float& g, float& b, float& a) {
    r = 0.102f;
    g = 0.106f;
    b = 0.122f;
    a = 1.0f;
    auto take = [&](bool overlay) -> bool {
        for (int n = static_cast<int>(s.count) - 1; n >= 0; --n) {
            const auto id = static_cast<uint16_t>(n);
            if (uiIsOverlay(s, id) != overlay || !nodeShown(s, id)) {
                continue;
            }
            const UiPaint* paint = s.ent[id].try_get<UiPaint>();
            if (paint == nullptr || paint->role == static_cast<uint8_t>(UiRole::Hidden)) {
                continue;
            }
            const UiBox& box = s.box[id];
            if (!hitBox(box, x, y)) {
                continue;
            }
            if (paint->ramp == kRampColorWheel) {
                const float px = (x - box.x) / box.w * 2.0f - 1.0f;
                const float py = (y - box.y) / box.h * 2.0f - 1.0f;
                const float rad = std::sqrt(px * px + py * py);
                if (rad > 1.02f) {
                    continue;
                }
                float hue = std::atan2(py, px) * 0.15915494f + 0.5f;
                if (hue < 0.0f) {
                    hue += 1.0f;
                }
                const float sat = rad > 1.0f ? 1.0f : rad;
                hsvToRgb(hue, sat, 1.0f, r, g, b);
                a = 1.0f;
                return true;
            }
            if (paint->ramp == kRampColorSwatch) {
                r = uiColor(s).colorR;
                g = uiColor(s).colorG;
                b = uiColor(s).colorB;
                a = uiColor(s).colorA;
                return true;
            }
            if (paint->ramp == kRampColorValue) {
                float v = box.h > 1.0f ? 1.0f - (y - box.y) / box.h : 1.0f;
                if (v < 0.0f) {
                    v = 0.0f;
                }
                if (v > 1.0f) {
                    v = 1.0f;
                }
                hsvToRgb(uiColor(s).colorH, uiColor(s).colorS, v, r, g, b);
                a = 1.0f;
                return true;
            }
            if (paint->a <= 0.02f) {
                continue;
            }
            r = paint->r;
            g = paint->g;
            b = paint->b;
            a = paint->a;
            return true;
        }
        return false;
    };
    if (!take(true)) {
        take(false);
    }
}

void colorOnValue(void* user, uint16_t id, float val) {
    auto& s = *static_cast<UiState*>(user);
    UiColorPickerComponent& c = uiColor(s);
    bool fromHsv = false;
    if (id == c.colorRgb[0]) {
        c.colorR = val;
    } else if (id == c.colorRgb[1]) {
        c.colorG = val;
    } else if (id == c.colorRgb[2]) {
        c.colorB = val;
    } else if (id == c.colorAlpha) {
        c.colorA = val;
    } else if (id == c.colorHsv[0]) {
        c.colorH = val;
        fromHsv = true;
    } else if (id == c.colorHsv[1]) {
        c.colorS = val;
        fromHsv = true;
    } else if (id == c.colorHsv[2]) {
        c.colorV = val;
        fromHsv = true;
    } else {
        return;
    }
    applyColor(s, fromHsv);
}

void publishColor(UiState& s) {
    if (uiColor(s).book == nullptr) {
        return;
    }
    ColorBook& b = *uiColor(s).book;
    b.r = uiColor(s).colorR;
    b.g = uiColor(s).colorG;
    b.b = uiColor(s).colorB;
    b.a = uiColor(s).colorA;
    b.h = uiColor(s).colorH;
    b.s = uiColor(s).colorS;
    b.v = uiColor(s).colorV;
    b.useAlpha = uiColor(s).colorUseAlpha;
    b.mode = uiColor(s).colorMode;
    b.present = uiColor(s).colorPresent;
    b.drop = uiColor(s).colorDrop;
    ++b.gen;
    uiColor(s).colorSeen = b.gen;
}

void setBtnLabel(UiState& s, uint16_t id, const char* text) {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    addText(s, id, text);
    const UiText* label = s.ent[id].try_get<UiText>();
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (label != nullptr && flex != nullptr) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Px);
        flex->width = static_cast<float>(label->len) * s.look.advance + 20.0f;
    }
}

void refreshColorChrome(UiState& s) {
    setBtnLabel(s, uiColor(s).colorMenuBtn, uiColor(s).colorMode == 0 ? "MENU *" : "MENU");
    setBtnLabel(s, uiColor(s).colorWinBtn, uiColor(s).colorMode == 1 ? "WINDOW *" : "WINDOW");
}

void placeColorMenu(UiState& s) {
    float x = 24.0f;
    float y = 48.0f;
    const float w = 500.0f;
    const float h = 720.0f;
    if (uiColor(s).colorAnchor != kUiNone && uiColor(s).colorAnchor < s.count) {
        const UiBox& b = s.box[uiColor(s).colorAnchor];
        x = b.x;
        y = b.y + b.h + 6.0f;
        if (s.laidH > 0.0f && y + h > s.laidH - 8.0f) {
            y = b.y - h - 6.0f;
        }
    }
    if (s.laidW > 0.0f && x + w > s.laidW - 8.0f) {
        x = s.laidW - w - 8.0f;
    }
    if (x < 8.0f) {
        x = 8.0f;
    }
    if (y < 8.0f) {
        y = 8.0f;
    }
    uiPlace(s, uiColor(s).colorPanel, true, x, y, w, h);
}

void syncColor(UiState& s) {
    if (uiColor(s).book == nullptr) {
        return;
    }
    const ColorBook& b = *uiColor(s).book;
    if (b.gen != uiColor(s).colorSeen) {
        uiColor(s).colorR = b.r;
        uiColor(s).colorG = b.g;
        uiColor(s).colorB = b.b;
        uiColor(s).colorA = b.a;
        uiColor(s).colorH = b.h;
        uiColor(s).colorS = b.s;
        uiColor(s).colorV = b.v;
        uiColor(s).colorUseAlpha = b.useAlpha;
        uiColor(s).colorMode = b.mode;
        uiColor(s).colorPresent = b.present;
        uiColor(s).colorDrop = b.drop;
        uiColor(s).colorSeen = b.gen;
        applyColor(s, false, false);
    }
    if (uiColor(s).colorPresent == uiColor(s).colorApplied) {
        return;
    }
    uiColor(s).colorApplied = uiColor(s).colorPresent;
    if (uiColor(s).colorHost == 0) {
        if (uiColor(s).colorPresent == 1) {
            placeColorMenu(s);
            uiShow(s, uiColor(s).colorPanel, true);
        } else {
            uiShow(s, uiColor(s).colorPanel, false);
            if (uiColor(s).colorPresent == 2 && uiColor(s).book != nullptr) {
                uiColor(s).book->present = 2;
            }
        }
    } else if (uiColor(s).colorPresent != 2) {
        s.opClose = true;
    }
}

void colorOnOpen(void* user, uint16_t id, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorAnchor = id;
    paintColor(s);
    if (uiColor(s).colorMode == 1) {
        uiColor(s).colorPresent = 2;
        uiColor(s).colorApplied = 2;
        uiShow(s, uiColor(s).colorPanel, false);
        if (uiColor(s).book != nullptr) {
            uiColor(s).book->present = 2;
        }
    } else {
        uiColor(s).colorPresent = 1;
        uiColor(s).colorApplied = 1;
        placeColorMenu(s);
        uiShow(s, uiColor(s).colorPanel, true);
    }
    publishColor(s);
}

void colorOnClose(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorPresent = 0;
    uiColor(s).colorApplied = 0;
    uiShow(s, uiColor(s).colorPanel, false);
    publishColor(s);
    if (uiColor(s).colorHost != 0) {
        s.opClose = true;
    }
}

void colorOnDrop(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorDrop = uiColor(s).colorDrop == 0 ? 1 : 0;
    publishColor(s);
    if (uiColor(s).colorHost == 0 && uiColor(s).colorPresent == 1) {
        if (uiColor(s).colorDrop == 0) {
            placeColorMenu(s);
        }
        uiShow(s, uiColor(s).colorPanel, uiColor(s).colorDrop == 0);
    }
}

void colorOnMenu(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorMode = 0;
    uiColor(s).colorPresent = 1;
    uiColor(s).colorApplied = 1;
    publishColor(s);
    refreshColorChrome(s);
    if (uiColor(s).colorHost != 0) {
        s.opClose = true;
    } else {
        placeColorMenu(s);
        uiShow(s, uiColor(s).colorPanel, true);
    }
}

void colorOnWindow(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorMode = 1;
    uiColor(s).colorPresent = 2;
    uiColor(s).colorApplied = 2;
    uiShow(s, uiColor(s).colorPanel, false);
    publishColor(s);
    refreshColorChrome(s);
    if (uiColor(s).colorHost == 0 && uiColor(s).book != nullptr) {
        uiColor(s).book->present = 2;
    }
}

void colorOnDrag(void* user, uint16_t id, float x, float y) {
    dragWheel(*static_cast<UiState*>(user), id, x, y);
}

void colorOnEdit(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiField* field = s.ent[id].try_get<UiField>();
    if (field == nullptr) {
        return;
    }
    if (id == uiColor(s).colorHex) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = uiColor(s).colorA;
        if (readHex(*field, r, g, b, a)) {
            uiColor(s).colorR = r;
            uiColor(s).colorG = g;
            uiColor(s).colorB = b;
            uiColor(s).colorA = a;
            applyColor(s, false);
        }
        return;
    }
    int which = -1;
    for (int i = 0; i < 7; ++i) {
        if (uiColor(s).colorNum[i] == id) {
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
            uiColor(s).colorR = u;
        } else if (which == 1) {
            uiColor(s).colorG = u;
        } else if (which == 2) {
            uiColor(s).colorB = u;
        } else {
            uiColor(s).colorA = u;
        }
        applyColor(s, false);
    } else if (which == 4) {
        if (n > 360) {
            n = 360;
        }
        uiColor(s).colorH = static_cast<float>(n) / 360.0f;
        applyColor(s, true);
    } else {
        if (n > 100) {
            n = 100;
        }
        const float u = static_cast<float>(n) / 100.0f;
        if (which == 5) {
            uiColor(s).colorS = u;
        } else {
            uiColor(s).colorV = u;
        }
        applyColor(s, true);
    }
}

bool colorGrab(void* user, uint16_t hit, float x, float y) {
    auto& s = *static_cast<UiState*>(user);
    const bool dropping = uiColor(s).colorDrop != 0 || (uiColor(s).book != nullptr && uiColor(s).book->drop != 0);
    if (!dropping) {
        return false;
    }
    if (hit == uiColor(s).colorPickBtn) {
        return false;
    }
    float sr = 0.0f;
    float sg = 0.0f;
    float sb = 0.0f;
    float sa = 1.0f;
    samplePixel(s, x, y, sr, sg, sb, sa);
    uiColor(s).colorR = sr;
    uiColor(s).colorG = sg;
    uiColor(s).colorB = sb;
    uiColor(s).colorA = sa;
    uiColor(s).colorDrop = 0;
    applyColor(s, false);
    if (uiColor(s).colorHost == 0 && uiColor(s).colorPresent == 1) {
        placeColorMenu(s);
        uiShow(s, uiColor(s).colorPanel, true);
    }
    return true;
}

void fillColorBody(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = 1;
    auto chip = [&](uint16_t host, const char* name, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 28.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, host, flex, paintToken(s.look, UiRole::Button, UiToken::Button, 6.0f));
        UiText label{};
        copyText(label.bytes, label.len, name);
        s.ent[id].set<UiText>(label);
        auto* box = s.ent[id].try_get_mut<UiFlex>();
        if (box != nullptr) {
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = static_cast<float>(label.len) * s.look.advance + 20.0f;
            box->height = 28.0f;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
        }
        uiBind(s, id, fn, &s);
        return id;
    };
    UiFlex wheelRow = row;
    wheelRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    wheelRow.height = 220.0f;
    wheelRow.shrink = 0.0f;
    wheelRow.gap = 10.0f;
    wheelRow.align = 1;
    const uint16_t wheelRowId = spawn(s, parent, wheelRow, paint(UiRole::Hidden, 0, 0, 0));
    UiFlex wheel{};
    wheel.widthMode = static_cast<uint8_t>(UiSize::Px);
    wheel.heightMode = static_cast<uint8_t>(UiSize::Px);
    wheel.width = 220.0f;
    wheel.height = 220.0f;
    wheel.shrink = 0.0f;
    uiColor(s).colorWheel = spawn(s, wheelRowId, wheel, colorRampPart(s.look, UiRole::Panel, UiToken::Content, kRampColorWheel));
    uiDrag(s, uiColor(s).colorWheel, colorOnDrag, &s);
    UiFlex valBar{};
    valBar.widthMode = static_cast<uint8_t>(UiSize::Px);
    valBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    valBar.width = 22.0f;
    valBar.height = 220.0f;
    valBar.shrink = 0.0f;
    uiColor(s).colorVal = spawn(s, wheelRowId, valBar, colorRampPart(s.look, UiRole::Panel, UiToken::Content, kRampColorValue));
    uiDrag(s, uiColor(s).colorVal, colorOnDrag, &s);
    UiFlex side{};
    side.direction = 0;
    side.widthMode = static_cast<uint8_t>(UiSize::Px);
    side.width = 96.0f;
    side.heightMode = static_cast<uint8_t>(UiSize::Px);
    side.height = 220.0f;
    side.gap = 8.0f;
    side.shrink = 0.0f;
    const uint16_t sideId = spawn(s, wheelRowId, side, paint(UiRole::Hidden, 0, 0, 0));
    UiFlex swatch{};
    swatch.heightMode = static_cast<uint8_t>(UiSize::Px);
    swatch.height = 72.0f;
    swatch.widthMode = static_cast<uint8_t>(UiSize::Percent);
    swatch.width = 100.0f;
    swatch.shrink = 0.0f;
    {
        UiPaint swatchPaint = colorRampPart(s.look, UiRole::Panel, UiToken::Preview, kRampColorSwatch, 6.0f);
        swatchPaint.r = uiColor(s).colorR;
        swatchPaint.g = uiColor(s).colorG;
        swatchPaint.b = uiColor(s).colorB;
        uiColor(s).colorSwatch = spawn(s, sideId, swatch, swatchPaint);
    }
    UiFlex pick{};
    pick.heightMode = static_cast<uint8_t>(UiSize::Px);
    pick.height = 32.0f;
    pick.widthMode = static_cast<uint8_t>(UiSize::Percent);
    pick.width = 100.0f;
    pick.shrink = 0.0f;
    UiPaint pickPaint = paintToken(s.look, UiRole::Button, UiToken::Button, 6.0f);
    pickPaint.icon = 7;
    const uint16_t pickId = spawn(s, sideId, pick, pickPaint);
    uiColor(s).colorPickBtn = pickId;
    UiText pickLabel{};
    copyText(pickLabel.bytes, pickLabel.len, "PICK");
    s.ent[pickId].set<UiText>(pickLabel);
    uiBind(s, pickId, colorOnDrop, &s);
    const char* names[7] = {"R", "G", "B", "A", "H", "S", "V"};
    const float starts[7] = {uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorA, uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV};
    for (int i = 0; i < 7; ++i) {
        UiFlex line = row;
        line.heightMode = static_cast<uint8_t>(UiSize::Px);
        line.height = 26.0f;
        line.shrink = 0.0f;
        line.align = 1;
        line.gap = 8.0f;
        const uint16_t lineId = spawn(s, parent, line, paint(UiRole::Hidden, 0, 0, 0));
        const uint16_t lab = spawn(s, lineId, UiFlex{}, paintToken(s.look, UiRole::Label, UiToken::Text, 0.0f));
        addText(s, lab, names[i]);
        UiFlex slide{};
        slide.grow = 1.0f;
        slide.shrink = 1.0f;
        slide.heightMode = static_cast<uint8_t>(UiSize::Px);
        slide.height = 18.0f;
        const uint16_t sliderId = spawn(s, lineId, slide, paintToken(s.look, UiRole::Slider, UiToken::Slider, 8.0f));
        const uint8_t ramps[7] = {1, 2, 3, 7, 4, 5, 6};
        if (auto* slidePaint = s.ent[sliderId].try_get_mut<UiPaint>()) {
            slidePaint->ramp = ramps[i];
        }
        s.ent[sliderId].set<UiRange>({starts[i], 0.0f, 1.0f});
        if (i < 3) {
            uiColor(s).colorRgb[i] = sliderId;
        } else if (i == 3) {
            uiColor(s).colorAlpha = sliderId;
        } else {
            uiColor(s).colorHsv[i - 4] = sliderId;
        }
        UiFlex num{};
        num.widthMode = static_cast<uint8_t>(UiSize::Px);
        num.heightMode = static_cast<uint8_t>(UiSize::Px);
        num.width = 56.0f;
        num.height = 24.0f;
        num.shrink = 0.0f;
        const uint16_t numId = spawn(s, lineId, num, paintToken(s.look, UiRole::Field, UiToken::Field, 4.0f));
        UiField digits{};
        digits.padL = 6.0f;
        digits.padR = 4.0f;
        digits.filter = 1;
        digits.maxChars = 3;
        s.ent[numId].set<UiField>(digits);
        uiEdit(s, numId, colorOnEdit, &s);
        uiColor(s).colorNum[i] = numId;
    }
    UiFlex hexFlex{};
    hexFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    hexFlex.height = 32.0f;
    hexFlex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    hexFlex.width = 100.0f;
    hexFlex.shrink = 0.0f;
    uiColor(s).colorHex = spawn(s, parent, hexFlex, paintToken(s.look, UiRole::Field, UiToken::Field, 6.0f));
    UiField hex{};
    hex.padL = 10.0f;
    hex.padR = 10.0f;
    hex.maxChars = 9;
    s.ent[uiColor(s).colorHex].set<UiField>(hex);
    uiEdit(s, uiColor(s).colorHex, colorOnEdit, &s);
    UiFlex modeRow = row;
    modeRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    modeRow.height = 32.0f;
    modeRow.shrink = 0.0f;
    modeRow.gap = 6.0f;
    const uint16_t modeId = spawn(s, parent, modeRow, paint(UiRole::Hidden, 0, 0, 0));
    uiColor(s).colorMenuBtn = chip(modeId, "MENU", colorOnMenu);
    uiColor(s).colorWinBtn = chip(modeId, "WINDOW", colorOnWindow);
    chip(parent, "CLOSE", colorOnClose);
    s.grabFn = colorGrab;
    s.grabUser = &s;
    applyColor(s, true, uiColor(s).colorSeen == 0);
}

void uiBuildColor(UiState& s) {
    uiColor(s).colorHost = 1;
    if (uiColor(s).book != nullptr && uiColor(s).book->gen != 0) {
        takeBook(s);
    } else {
        rgbToHsv(uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV);
    }
    UiFlex col{};
    col.direction = 0;
    col.shrink = 1.0f;
    UiFlex row = col;
    row.direction = 1;
    UiFlex rootF = col;
    rootF.pad = 12.0f;
    rootF.gap = 8.0f;
    const uint16_t root = spawn(s, kUiNone, rootF, paintToken(s.look, UiRole::Panel, UiToken::Menu, 0.0f));
    UiFlex titleF = row;
    titleF.heightMode = static_cast<uint8_t>(UiSize::Px);
    titleF.height = 36.0f;
    titleF.shrink = 0.0f;
    titleF.align = 1;
    const uint16_t title = spawn(s, root, titleF, paintToken(s.look, UiRole::Panel, UiToken::Title, 0.0f));
    const uint16_t brand = spawn(s, title, UiFlex{}, paintToken(s.look, UiRole::Label, UiToken::Text, 0.0f));
    addText(s, brand, "COLOR");
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = static_cast<uint8_t>(UiSize::Px);
    gap.height = 1.0f;
    spawn(s, title, gap, paint(UiRole::Hidden, 0, 0, 0));
    spawn(s, title, px(28.0f, 28.0f), paintToken(s.look, UiRole::Close, UiToken::Close, 6.0f));
    uiColor(s).colorPanel = root;
    uiColor(s).colorPresent = 2;
    uiColor(s).colorApplied = 2;
    uiColor(s).colorMode = 1;
    fillColorBody(s, root);
}

bool colorCustomDraw(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap) {
    const UiPaint* paintPtr = id < s.count ? s.ent[id].try_get<UiPaint>() : nullptr;
    if (paintPtr == nullptr) {
        return false;
    }
    const UiPaint& paint = *paintPtr;
    const uint8_t ramp = paint.ramp;
    if (ramp != kRampColorWheel && ramp != kRampColorValue && ramp != kRampColorSwatch && ramp != kRampColorAlpha) {
        return false;
    }
    const UiBox& box = s.box[id];
        if (ramp == kRampColorWheel) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, 1.0f, 1.0f, 1.0f, 0, kUiFlagWheel);
            const float radius = std::min(box.w, box.h) * 0.5f;
            const float ang = (uiColor(s).colorH - 0.5f) * 6.2831853f;
            const float mx = box.x + box.w * 0.5f + std::cos(ang) * uiColor(s).colorS * radius;
            const float my = box.y + box.h * 0.5f + std::sin(ang) * uiColor(s).colorS * radius;
            n = emitRect(dst, n, cap, mx - 7.0f, my - 7.0f, 14.0f, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f, 7.0f, nullptr);
            n = emitRect(dst, n, cap, mx - 4.0f, my - 4.0f, 8.0f, 8.0f, uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, 1.0f, 4.0f, nullptr);
            return true;
        }
        if (ramp == kRampColorValue) {
            float pr = 0.0f;
            float pg = 0.0f;
            float pb = 0.0f;
            const float hh = uiColor(s).colorH * 6.0f;
            const int hi = static_cast<int>(hh) % 6;
            const float f = hh - static_cast<float>(static_cast<int>(hh));
            const float pv = 1.0f;
            const float q = pv * (1.0f - f * uiColor(s).colorS);
            const float t = pv * (1.0f - (1.0f - f) * uiColor(s).colorS);
            const float pp = pv * (1.0f - uiColor(s).colorS);
            if (hi == 0) { pr = pv; pg = t; pb = pp; }
            else if (hi == 1) { pr = q; pg = pv; pb = pp; }
            else if (hi == 2) { pr = pp; pg = pv; pb = t; }
            else if (hi == 3) { pr = pp; pg = q; pb = pv; }
            else if (hi == 4) { pr = t; pg = pp; pb = pv; }
            else { pr = pv; pg = pp; pb = q; }
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, pr, pg, pb, 1, kUiFlagRamp);
            const float ty = box.y + (1.0f - uiColor(s).colorV) * box.h;
            n = emitRect(dst, n, cap, box.x - 2.0f, ty - 3.0f, box.w + 4.0f, 6.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, nullptr);
            return true;
        }
        if (ramp == kRampColorSwatch) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, 0.0f, 0.0f, 0.0f, 6, kUiFlagRamp);
            n = emitRect(dst, n, cap, box.x, box.y, box.w, box.h, uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorA, paint.radius, nullptr);
            return true;
        }
        if (ramp == kRampColorAlpha) {
            n = emitRamp(dst, n, cap, box.x, box.y, box.w, box.h, uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, 2, kUiFlagRamp);
            const float tx = box.x + uiColor(s).colorA * box.w;
            n = emitRect(dst, n, cap, tx - 3.0f, box.y - 2.0f, 6.0f, box.h + 4.0f, 1.0f, 1.0f, 1.0f, 1.0f, 2.0f, nullptr);
            return true;
        }
    return false;
}

uint16_t loadColorBin(UiState& s, uint16_t parent, const BinNode& node, const char* name, void*) {
    UiFlex flex{};
    flex.position = 1;
    flex.posX = node.posX;
    flex.posY = node.posY;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = node.width;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = node.height;
    flex.pad = 10.0f;
    flex.gap = 6.0f;
    flex.shrink = 0.0f;
    const uint16_t id = spawn(s, parent, flex, paint(UiRole::Panel, node.r, node.g, node.b, node.radius));
    if (uiColor(s).book == nullptr || uiColor(s).book->gen == 0) {
        rgbToHsv(uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV);
        uiColor(s).colorA = 1.0f;
        uiColor(s).colorUseAlpha = 1;
    } else {
        takeBook(s);
    }
    uiColor(s).colorPanel = id;
    fillColorBody(s, id);
    uiShow(s, id, false);
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    return id;
}

void uiInstallColorBin(UiState& s) {
    uiRegisterCustomBinNode(s, 12, loadColorBin, nullptr);
    uiDrawAdd(s, colorCustomDraw);
    for (uint8_t i = 0; i < s.valueN; ++i) {
        if (s.valueFn[i] == colorOnValue) {
            return;
        }
    }
    uiListen(s, colorOnValue, &s);
}

} // namespace burnhope

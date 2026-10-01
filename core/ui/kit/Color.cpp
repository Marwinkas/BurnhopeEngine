#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    float hh = h * 6.0f;
    if (hh >= 6.0f) {
        hh = 0.0f;
    }
    const int i = static_cast<int>(hh);
    const float f = hh - static_cast<float>(i);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    if (i == 0) { r = v; g = t; b = p; }
    else if (i == 1) { r = q; g = v; b = p; }
    else if (i == 2) { r = p; g = v; b = t; }
    else if (i == 3) { r = p; g = q; b = v; }
    else if (i == 4) { r = t; g = p; b = v; }
    else { r = v; g = p; b = q; }
}

void rgbToHsv(float r, float g, float b, float& h, float& s, float& v) {
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

void setRange(UiState& s, uint16_t id, float value) {
    if (id == kUiNone) {
        return;
    }
    auto* range = s.ent[id].try_get_mut<UiRange>();
    if (range != nullptr) {
        range->value = value;
    }
}

int hexNib(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
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

void applyColor(UiState& s, bool fromHsv, bool publish) {
    if (fromHsv) {
        hsvToRgb(s.colorH, s.colorS, s.colorV, s.colorR, s.colorG, s.colorB);
    } else {
        rgbToHsv(s.colorR, s.colorG, s.colorB, s.colorH, s.colorS, s.colorV);
    }
    setRange(s, s.colorRgb[0], s.colorR);
    setRange(s, s.colorRgb[1], s.colorG);
    setRange(s, s.colorRgb[2], s.colorB);
    setRange(s, s.colorHsv[0], s.colorH);
    setRange(s, s.colorHsv[1], s.colorS);
    setRange(s, s.colorHsv[2], s.colorV);
    if (s.colorSwatch != kUiNone) {
        auto* paint = s.ent[s.colorSwatch].try_get_mut<UiPaint>();
        if (paint != nullptr) {
            paint->r = s.colorR;
            paint->g = s.colorG;
            paint->b = s.colorB;
            paint->a = s.colorA;
        }
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* ramp = s.ent[id].try_get_mut<UiPaint>();
        if (ramp != nullptr && ramp->ramp >= 5) {
            ramp->r = s.colorR;
            ramp->g = s.colorG;
            ramp->b = s.colorB;
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
        writeHex(field->bytes, field->len, s.colorR, s.colorG, s.colorB, s.colorA, true);
        field->caret = field->len;
        field->anchor = field->len;
    };
    write(s.colorTarget, false);
    write(s.colorHex, true);
    const int shown[7] = {
        static_cast<int>(s.colorR * 255.0f + 0.5f),
        static_cast<int>(s.colorG * 255.0f + 0.5f),
        static_cast<int>(s.colorB * 255.0f + 0.5f),
        static_cast<int>(s.colorA * 255.0f + 0.5f),
        static_cast<int>(s.colorH * 360.0f + 0.5f),
        static_cast<int>(s.colorS * 100.0f + 0.5f),
        static_cast<int>(s.colorV * 100.0f + 0.5f),
    };
    for (int i = 0; i < 7; ++i) {
        const uint16_t id = s.colorNum[i];
        if (id == kUiNone) {
            continue;
        }
        auto* field = s.ent[id].try_get_mut<UiField>();
        if (field == nullptr || s.focused == id) {
            continue;
        }
        writeInt(field->bytes, field->len, shown[i]);
        field->caret = field->len;
        field->anchor = field->len;
    }
    setRange(s, s.colorAlpha, s.colorA);
    if (publish) {
        publishColor(s);
    }
    refreshColorChrome(s);
    s.visualDirty = true;
}

void takeBook(UiState& s) {
    if (s.book == nullptr || s.book->gen == 0) {
        return;
    }
    s.colorR = s.book->r;
    s.colorG = s.book->g;
    s.colorB = s.book->b;
    s.colorA = s.book->a;
    s.colorH = s.book->h;
    s.colorS = s.book->s;
    s.colorV = s.book->v;
    s.colorUseAlpha = s.book->useAlpha;
    s.colorMode = s.book->mode;
    s.colorPresent = s.book->present;
    s.colorDrop = s.book->drop;
    s.colorSeen = s.book->gen;
    s.colorApplied = s.book->present;
}

void paintColor(UiState& s) {
    applyColor(s, false);
}

void dragWheel(UiState& s, uint16_t id, float x, float y) {
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    const UiBox& b = s.box[id];
    if (paint->tag == -70) {
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
        s.colorH = hue;
        s.colorS = sat;
        applyColor(s, true);
    } else if (paint->tag == -71) {
        float t = b.h > 1.0f ? 1.0f - (y - b.y) / b.h : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        s.colorV = t;
        applyColor(s, true);
    } else if (paint->tag == -72) {
        float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        s.colorA = t;
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
        if (hexNib(field.bytes[i]) < 0) {
            return false;
        }
        digits[n++] = field.bytes[i];
    }
    if (n != 6 && n != 8) {
        return false;
    }
    auto chan = [&](int at) {
        return (hexNib(digits[at]) * 16 + hexNib(digits[at + 1])) / 255.0f;
    };
    r = chan(0);
    g = chan(2);
    b = chan(4);
    if (n == 8) {
        a = chan(6);
    }
    return true;
}

void writeInt(char* dst, uint8_t& len, int v) {
    if (v < 0) {
        v = 0;
    }
    if (v > 999) {
        v = 999;
    }
    char rev[4]{};
    int n = 0;
    int x = v;
    if (x == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        len = 1;
        return;
    }
    while (x > 0 && n < 3) {
        rev[n++] = static_cast<char>('0' + (x % 10));
        x /= 10;
    }
    for (int i = 0; i < n; ++i) {
        dst[i] = rev[n - 1 - i];
    }
    dst[n] = '\0';
    len = static_cast<uint8_t>(n);
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

bool nodeShown(const UiState& s, uint16_t id) {
    for (uint16_t p = id; p != kUiNone; p = s.parentOf[p]) {
        if (s.yoga[p] != nullptr && YGNodeStyleGetDisplay(s.yoga[p]) == YGDisplayNone) {
            return false;
        }
    }
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
            if (paint->tag == -70) {
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
            if (paint->tag == -78) {
                r = s.colorR;
                g = s.colorG;
                b = s.colorB;
                a = s.colorA;
                return true;
            }
            if (paint->tag == -71) {
                float v = box.h > 1.0f ? 1.0f - (y - box.y) / box.h : 1.0f;
                if (v < 0.0f) {
                    v = 0.0f;
                }
                if (v > 1.0f) {
                    v = 1.0f;
                }
                hsvToRgb(s.colorH, s.colorS, v, r, g, b);
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

void colorTakeSlider(UiState& s, uint16_t id) {
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    const UiRange* range = s.ent[id].try_get<UiRange>();
    if (paint == nullptr || range == nullptr || paint->tag > -40 || paint->tag < -46) {
        return;
    }
    if (paint->tag == -40) {
        s.colorR = range->value;
    } else if (paint->tag == -41) {
        s.colorG = range->value;
    } else if (paint->tag == -42) {
        s.colorB = range->value;
    } else if (paint->tag == -46) {
        s.colorA = range->value;
    } else if (paint->tag == -43) {
        s.colorH = range->value;
    } else if (paint->tag == -44) {
        s.colorS = range->value;
    } else {
        s.colorV = range->value;
    }
    applyColor(s, paint->tag <= -43 && paint->tag >= -45);
}

void colorOnValue(void* user, uint16_t id, float) {
    colorTakeSlider(*static_cast<UiState*>(user), id);
}

void publishColor(UiState& s) {
    if (s.book == nullptr) {
        return;
    }
    ColorBook& b = *s.book;
    b.r = s.colorR;
    b.g = s.colorG;
    b.b = s.colorB;
    b.a = s.colorA;
    b.h = s.colorH;
    b.s = s.colorS;
    b.v = s.colorV;
    b.useAlpha = s.colorUseAlpha;
    b.mode = s.colorMode;
    b.present = s.colorPresent;
    b.drop = s.colorDrop;
    ++b.gen;
    s.colorSeen = b.gen;
}

void setBtnLabel(UiState& s, uint16_t id, const char* text) {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    auto* label = s.ent[id].try_get_mut<UiText>();
    if (label == nullptr) {
        return;
    }
    copyText(label->bytes, label->len, text);
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex != nullptr) {
        flex->widthMode = static_cast<uint8_t>(UiSize::Px);
        flex->width = static_cast<float>(label->len) * s.look.advance + 20.0f;
    }
}

void refreshColorChrome(UiState& s) {
    setBtnLabel(s, s.colorMenuBtn, s.colorMode == 0 ? "MENU *" : "MENU");
    setBtnLabel(s, s.colorWinBtn, s.colorMode == 1 ? "WINDOW *" : "WINDOW");
}

void placeColorMenu(UiState& s) {
    float x = 24.0f;
    float y = 48.0f;
    const float w = 500.0f;
    const float h = 720.0f;
    if (s.colorAnchor != kUiNone && s.colorAnchor < s.count) {
        const UiBox& b = s.box[s.colorAnchor];
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
    uiPlace(s, s.colorPanel, true, x, y, w, h);
}

void syncColor(UiState& s) {
    if (s.book == nullptr) {
        return;
    }
    const ColorBook& b = *s.book;
    if (b.gen != s.colorSeen) {
        s.colorR = b.r;
        s.colorG = b.g;
        s.colorB = b.b;
        s.colorA = b.a;
        s.colorH = b.h;
        s.colorS = b.s;
        s.colorV = b.v;
        s.colorUseAlpha = b.useAlpha;
        s.colorMode = b.mode;
        s.colorPresent = b.present;
        s.colorDrop = b.drop;
        s.colorSeen = b.gen;
        applyColor(s, false, false);
    }
    if (s.colorPresent == s.colorApplied) {
        return;
    }
    s.colorApplied = s.colorPresent;
    if (s.colorHost == 0) {
        if (s.colorPresent == 1) {
            placeColorMenu(s);
            uiShow(s, s.colorPanel, true);
        } else {
            uiShow(s, s.colorPanel, false);
            if (s.colorPresent == 2) {
                s.opColor = true;
            }
        }
    } else if (s.colorPresent != 2) {
        s.opClose = true;
    }
}

void colorOnOpen(void* user, uint16_t id, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    s.colorAnchor = id;
    paintColor(s);
    if (s.colorMode == 1) {
        s.colorPresent = 2;
        s.colorApplied = 2;
        uiShow(s, s.colorPanel, false);
        s.opColor = true;
    } else {
        s.colorPresent = 1;
        s.colorApplied = 1;
        placeColorMenu(s);
        uiShow(s, s.colorPanel, true);
    }
    publishColor(s);
}

void colorOnClose(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    s.colorPresent = 0;
    s.colorApplied = 0;
    uiShow(s, s.colorPanel, false);
    publishColor(s);
    if (s.colorHost != 0) {
        s.opClose = true;
    }
}

void colorOnDrop(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    s.colorDrop = s.colorDrop == 0 ? 1 : 0;
    publishColor(s);
    if (s.colorHost == 0 && s.colorPresent == 1) {
        if (s.colorDrop == 0) {
            placeColorMenu(s);
        }
        uiShow(s, s.colorPanel, s.colorDrop == 0);
    }
}

void colorOnMenu(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    s.colorMode = 0;
    s.colorPresent = 1;
    s.colorApplied = 1;
    publishColor(s);
    refreshColorChrome(s);
    if (s.colorHost != 0) {
        s.opClose = true;
    } else {
        placeColorMenu(s);
        uiShow(s, s.colorPanel, true);
    }
}

void colorOnWindow(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    s.colorMode = 1;
    s.colorPresent = 2;
    s.colorApplied = 2;
    uiShow(s, s.colorPanel, false);
    publishColor(s);
    refreshColorChrome(s);
    if (s.colorHost == 0) {
        s.opColor = true;
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
    if (id == s.colorHex) {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = s.colorA;
        if (readHex(*field, r, g, b, a)) {
            s.colorR = r;
            s.colorG = g;
            s.colorB = b;
            s.colorA = a;
            applyColor(s, false);
        }
        return;
    }
    int which = -1;
    for (int i = 0; i < 7; ++i) {
        if (s.colorNum[i] == id) {
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
            s.colorR = u;
        } else if (which == 1) {
            s.colorG = u;
        } else if (which == 2) {
            s.colorB = u;
        } else {
            s.colorA = u;
        }
        applyColor(s, false);
    } else if (which == 4) {
        if (n > 360) {
            n = 360;
        }
        s.colorH = static_cast<float>(n) / 360.0f;
        applyColor(s, true);
    } else {
        if (n > 100) {
            n = 100;
        }
        const float u = static_cast<float>(n) / 100.0f;
        if (which == 5) {
            s.colorS = u;
        } else {
            s.colorV = u;
        }
        applyColor(s, true);
    }
}

bool colorGrab(void* user, uint16_t hit, float x, float y) {
    auto& s = *static_cast<UiState*>(user);
    const bool dropping = s.colorDrop != 0 || (s.book != nullptr && s.book->drop != 0);
    if (!dropping) {
        return false;
    }
    const UiPaint* picked = hit != kUiNone ? s.ent[hit].try_get<UiPaint>() : nullptr;
    if (picked != nullptr && picked->tag == -76) {
        return false;
    }
    float sr = 0.0f;
    float sg = 0.0f;
    float sb = 0.0f;
    float sa = 1.0f;
    samplePixel(s, x, y, sr, sg, sb, sa);
    s.colorR = sr;
    s.colorG = sg;
    s.colorB = sb;
    s.colorA = sa;
    s.colorDrop = 0;
    applyColor(s, false);
    if (s.colorHost == 0 && s.colorPresent == 1) {
        placeColorMenu(s);
        uiShow(s, s.colorPanel, true);
    }
    return true;
}

void fillColorBody(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = 1;
    auto chip = [&](uint16_t host, const char* name, int8_t tag, UiClickFn fn) {
        UiFlex flex{};
        flex.heightMode = static_cast<uint8_t>(UiSize::Px);
        flex.height = 28.0f;
        flex.shrink = 0.0f;
        const uint16_t id = spawn(s, host, flex, paint(UiRole::Button, 0.20f, 0.24f, 0.32f, 6.0f, tag));
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
    s.colorWheel = spawn(s, wheelRowId, wheel, paint(UiRole::Panel, 0, 0, 0, 0.0f, -70));
    uiDrag(s, s.colorWheel, colorOnDrag, &s);
    UiFlex valBar{};
    valBar.widthMode = static_cast<uint8_t>(UiSize::Px);
    valBar.heightMode = static_cast<uint8_t>(UiSize::Px);
    valBar.width = 22.0f;
    valBar.height = 220.0f;
    valBar.shrink = 0.0f;
    s.colorVal = spawn(s, wheelRowId, valBar, paint(UiRole::Panel, 0, 0, 0, 0.0f, -71));
    uiDrag(s, s.colorVal, colorOnDrag, &s);
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
    s.colorSwatch = spawn(s, sideId, swatch, paint(UiRole::Panel, s.colorR, s.colorG, s.colorB, 6.0f, -78));
    UiFlex pick{};
    pick.heightMode = static_cast<uint8_t>(UiSize::Px);
    pick.height = 32.0f;
    pick.widthMode = static_cast<uint8_t>(UiSize::Percent);
    pick.width = 100.0f;
    pick.shrink = 0.0f;
    UiPaint pickPaint = paint(UiRole::Button, 0.20f, 0.24f, 0.32f, 6.0f, -76);
    pickPaint.icon = 7;
    const uint16_t pickId = spawn(s, sideId, pick, pickPaint);
    UiText pickLabel{};
    copyText(pickLabel.bytes, pickLabel.len, "PICK");
    s.ent[pickId].set<UiText>(pickLabel);
    uiBind(s, pickId, colorOnDrop, &s);
    const char* names[7] = {"R", "G", "B", "A", "H", "S", "V"};
    const int8_t tags[7] = {-40, -41, -42, -46, -43, -44, -45};
    const int8_t numTags[7] = {-80, -81, -82, -83, -84, -85, -86};
    const float starts[7] = {s.colorR, s.colorG, s.colorB, s.colorA, s.colorH, s.colorS, s.colorV};
    for (int i = 0; i < 7; ++i) {
        UiFlex line = row;
        line.heightMode = static_cast<uint8_t>(UiSize::Px);
        line.height = 26.0f;
        line.shrink = 0.0f;
        line.align = 1;
        line.gap = 8.0f;
        const uint16_t lineId = spawn(s, parent, line, paint(UiRole::Hidden, 0, 0, 0));
        const uint16_t lab = spawn(s, lineId, UiFlex{}, paint(UiRole::Label, 0.75f, 0.80f, 0.88f));
        addText(s, lab, names[i]);
        UiFlex slide{};
        slide.grow = 1.0f;
        slide.shrink = 1.0f;
        slide.heightMode = static_cast<uint8_t>(UiSize::Px);
        slide.height = 18.0f;
        const uint16_t sliderId = spawn(s, lineId, slide, paint(UiRole::Slider, 0.16f, 0.17f, 0.20f, 8.0f, tags[i]));
        const uint8_t ramps[7] = {1, 2, 3, 7, 4, 5, 6};
        if (auto* slidePaint = s.ent[sliderId].try_get_mut<UiPaint>()) {
            slidePaint->ramp = ramps[i];
        }
        s.ent[sliderId].set<UiRange>({starts[i], 0.0f, 1.0f});
        if (i < 3) {
            s.colorRgb[i] = sliderId;
        } else if (i == 3) {
            s.colorAlpha = sliderId;
        } else {
            s.colorHsv[i - 4] = sliderId;
        }
        UiFlex num{};
        num.widthMode = static_cast<uint8_t>(UiSize::Px);
        num.heightMode = static_cast<uint8_t>(UiSize::Px);
        num.width = 56.0f;
        num.height = 24.0f;
        num.shrink = 0.0f;
        const uint16_t numId = spawn(s, lineId, num, paint(UiRole::Field, 0.10f, 0.11f, 0.14f, 4.0f, numTags[i]));
        UiField digits{};
        digits.padL = 6.0f;
        digits.padR = 4.0f;
        digits.filter = 1;
        digits.maxChars = 3;
        s.ent[numId].set<UiField>(digits);
        uiEdit(s, numId, colorOnEdit, &s);
        s.colorNum[i] = numId;
    }
    UiFlex hexFlex{};
    hexFlex.heightMode = static_cast<uint8_t>(UiSize::Px);
    hexFlex.height = 32.0f;
    hexFlex.widthMode = static_cast<uint8_t>(UiSize::Percent);
    hexFlex.width = 100.0f;
    hexFlex.shrink = 0.0f;
    s.colorHex = spawn(s, parent, hexFlex, paint(UiRole::Field, 0.10f, 0.11f, 0.14f, 6.0f));
    UiField hex{};
    hex.padL = 10.0f;
    hex.padR = 10.0f;
    hex.maxChars = 9;
    s.ent[s.colorHex].set<UiField>(hex);
    uiEdit(s, s.colorHex, colorOnEdit, &s);
    UiFlex modeRow = row;
    modeRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    modeRow.height = 32.0f;
    modeRow.shrink = 0.0f;
    modeRow.gap = 6.0f;
    const uint16_t modeId = spawn(s, parent, modeRow, paint(UiRole::Hidden, 0, 0, 0));
    s.colorMenuBtn = chip(modeId, "MENU", -74, colorOnMenu);
    s.colorWinBtn = chip(modeId, "WINDOW", -75, colorOnWindow);
    chip(parent, "CLOSE", -58, colorOnClose);
    s.grabFn = colorGrab;
    s.grabUser = &s;
    applyColor(s, true, s.colorSeen == 0);
}

void uiBuildColor(UiState& s) {
    s.colorHost = 1;
    if (s.book != nullptr && s.book->gen != 0) {
        takeBook(s);
    } else {
        rgbToHsv(s.colorR, s.colorG, s.colorB, s.colorH, s.colorS, s.colorV);
    }
    UiFlex col{};
    col.direction = 0;
    col.shrink = 1.0f;
    UiFlex row = col;
    row.direction = 1;
    UiFlex rootF = col;
    rootF.pad = 12.0f;
    rootF.gap = 8.0f;
    const uint16_t root = spawn(s, kUiNone, rootF, paint(UiRole::Panel, 0.10f, 0.11f, 0.14f));
    UiFlex titleF = row;
    titleF.heightMode = static_cast<uint8_t>(UiSize::Px);
    titleF.height = 36.0f;
    titleF.shrink = 0.0f;
    titleF.align = 1;
    const uint16_t title = spawn(s, root, titleF, paint(UiRole::Panel, 0.14f, 0.16f, 0.20f));
    const uint16_t brand = spawn(s, title, UiFlex{}, paint(UiRole::Label, 0.92f, 0.94f, 0.97f));
    addText(s, brand, "COLOR");
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = static_cast<uint8_t>(UiSize::Px);
    gap.height = 1.0f;
    spawn(s, title, gap, paint(UiRole::Hidden, 0, 0, 0));
    spawn(s, title, px(28.0f, 28.0f), paint(UiRole::Close, 0.55f, 0.28f, 0.28f, 6.0f));
    s.colorPanel = root;
    s.colorPresent = 2;
    s.colorApplied = 2;
    s.colorMode = 1;
    fillColorBody(s, root);
}

} // namespace burnhope

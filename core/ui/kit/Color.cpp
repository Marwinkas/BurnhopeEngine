#include "Str.hpp"
#include "ui/Icon.hpp"
#include "ui/UiBin.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Interaction.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Slider.hpp"
#include "color/Color.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {
namespace {
constexpr UiRamp kRampColorWheel = UiRamp::HueWheel;
constexpr UiRamp kRampColorValue = UiRamp::ValueRamp;
constexpr UiRamp kRampColorSwatch = UiRamp::Gradient;
constexpr UiRamp kRampColorAlpha = UiRamp::Alpha;
constexpr float kColorSide = 96.0f;
constexpr float kColorWheel = 220.0f;
constexpr float kColorVal = 22.0f;
constexpr float kColorGap = 10.0f;
constexpr float kColorPad = 12.0f;
constexpr float kColorTitle = 36.0f;
constexpr float kColorW = kColorPad * 2.0f + kColorWheel + kColorGap + kColorVal + kColorGap + kColorSide;
constexpr float kColorH = 634.0f;

UiPaint colorRampPart(const UiLook& look, UiToken token, UiRamp ramp, float radius = 0.0f) {
    UiPaint paint = paintToken(look, token, radius);
    paint.ramp = ramp;
    return paint;
}
} // namespace

void publishColor(UiState& s);
void refreshColorChrome(UiState& s);
void fillColorBody(UiState& s, uint16_t parent);
void colorOnClose(void* user, uint16_t);

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
    for (uint16_t slot : {uiColor(s).colorRgb[0], uiColor(s).colorRgb[1], uiColor(s).colorRgb[2], uiColor(s).colorAlpha, uiColor(s).colorHsv[0], uiColor(s).colorHsv[1], uiColor(s).colorHsv[2]}) {
        if (slot == kUiNone || slot >= s.count) {
            continue;
        }
        auto* ramp = s.ent[slot].try_get_mut<UiPaint>();
        if (ramp != nullptr) {
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
            if (paint == nullptr || paint->a <= 0.0f || s.ent[id].has<UiPass>()) {
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
    if (uiColor(s).onColor != nullptr && uiColor(s).colorPresent != 0 && uiColor(s).colorAnchor != kUiNone) {
        uiColor(s).onColor(uiColor(s).colorUser, s, uiColor(s).colorAnchor, b.r, b.g, b.b);
    }
}

void setBtnLabel(UiState& s, uint16_t id, const char* text) {
    if (id == kUiNone || id >= s.count) {
        return;
    }
    addText(s, id, text);
    const UiText* label = s.ent[id].try_get<UiText>();
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (label != nullptr && flex != nullptr) {
        flex->widthMode = UiSize::Px;
        flex->width = static_cast<float>(label->len) * s.look.advance + 20.0f;
    }
}

void refreshColorChrome(UiState& s) {
    setBtnLabel(s, uiColor(s).colorMenuBtn, uiColor(s).colorMode == 0 ? "MENU *" : "MENU");
    setBtnLabel(s, uiColor(s).colorWinBtn, uiColor(s).colorMode == 1 ? "WINDOW *" : "WINDOW");
}

void colorHead(UiState& s, uint16_t parent) {
    UiFlex titleF{};
    titleF.direction = UiFlexDir::Row;
    titleF.heightMode = UiSize::Px;
    titleF.height = kColorTitle;
    titleF.shrink = 0.0f;
    titleF.align = UiAlign::Center;
    titleF.padL = 12.0f;
    titleF.padR = 4.0f;
    const uint16_t title = uiPanel(s, parent, titleF, paintToken(s.look, UiToken::Title, 0.0f));
    if (title >= s.count) {
        return;
    }
    s.ent[title].add<UiTitleBar>();
    const uint16_t brand = uiLabel(s, title, "Color");
    (void)brand;
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = UiSize::Px;
    gap.height = 1.0f;
    uiPanel(s, title, gap, paintClear());
    const uint16_t close = uiButton(s, title, {.label = "", .width = 28.0f, .height = 28.0f, .onClick = colorOnClose, .user = &s});
    if (close < s.count) {
        if (auto* paint = s.ent[close].try_get_mut<UiPaint>()) {
            paint->icon = static_cast<uint8_t>(UiIcon::Dismiss);
            paint->token = UiToken::Close;
        }
    }
}

void placeColorMenu(UiState& s) {
    float x = 24.0f;
    float y = 48.0f;
    const float w = kColorW;
    const float h = kColorH;
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
        uiRequestClose(s);
    }
}

void uiEnsureColorPopup(UiState& s) {
    UiColorPickerComponent& picker = uiColor(s);
    if (picker.colorPanel < s.count && s.ent[picker.colorPanel].is_alive()) {
        return;
    }
    if (s.count == 0 || s.yoga[0] == nullptr) {
        return;
    }
    picker.colorHost = 0;
    if (picker.colorSeen == 0) {
        rgbToHsv(picker.colorR, picker.colorG, picker.colorB, picker.colorH, picker.colorS, picker.colorV);
    }
    UiFlex root{};
    root.position = UiPosition::Absolute;
    root.direction = UiFlexDir::Column;
    root.pad = 0.0f;
    root.gap = 0.0f;
    root.widthMode = UiSize::Px;
    root.width = kColorW;
    root.heightMode = UiSize::Px;
    root.height = kColorH;
    root.shrink = 0.0f;
    const uint16_t id = uiPanel(s, 0, root, paintToken(s.look, UiToken::Menu, 0.0f));
    if (id == kUiNone) {
        return;
    }
    picker.colorPanel = id;
    picker.colorPresent = 0;
    picker.colorApplied = 0;
    colorHead(s, id);
    UiFlex body{};
    body.direction = UiFlexDir::Column;
    body.pad = kColorPad;
    body.gap = 8.0f;
    body.grow = 1.0f;
    body.shrink = 1.0f;
    const uint16_t bodyId = uiPanel(s, id, body, paintClear());
    fillColorBody(s, bodyId);
    uiShow(s, id, false);
}

void colorOnOpen(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    uiEnsureColorPopup(s);
    if (uiColor(s).book != nullptr && uiColor(s).book->gen != uiColor(s).colorSeen) {
        takeBook(s);
    }
    uiColor(s).colorAnchor = id;
    applyColor(s, false, false);
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

void colorOnClose(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorPresent = 0;
    uiColor(s).colorApplied = 0;
    uiShow(s, uiColor(s).colorPanel, false);
    publishColor(s);
    if (uiColor(s).colorHost != 0) {
        uiRequestClose(s);
    }
}

void colorOnDrop(void* user, uint16_t) {
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

void colorOnMenu(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiColor(s).colorMode = 0;
    uiColor(s).colorPresent = 1;
    uiColor(s).colorApplied = 1;
    publishColor(s);
    refreshColorChrome(s);
    if (uiColor(s).colorHost != 0) {
        uiRequestClose(s);
    } else {
        placeColorMenu(s);
        uiShow(s, uiColor(s).colorPanel, true);
    }
}

void colorOnWindow(void* user, uint16_t) {
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
    row.direction = UiFlexDir::Row;
    UiFlex wheelRow = row;
    wheelRow.heightMode = UiSize::Px;
    wheelRow.height = 220.0f;
    wheelRow.shrink = 0.0f;
    wheelRow.gap = 10.0f;
    wheelRow.align = UiAlign::Center;
    const uint16_t wheelRowId = uiPanel(s, parent, wheelRow, paintClear());
    UiFlex wheel{};
    wheel.widthMode = UiSize::Px;
    wheel.heightMode = UiSize::Px;
    wheel.width = kColorWheel;
    wheel.height = kColorWheel;
    wheel.shrink = 0.0f;
    uiColor(s).colorWheel = spawn(s, wheelRowId, wheel, colorRampPart(s.look, UiToken::Content, kRampColorWheel));
    uiDrag(s, uiColor(s).colorWheel, colorOnDrag, &s);
    UiFlex valBar{};
    valBar.widthMode = UiSize::Px;
    valBar.heightMode = UiSize::Px;
    valBar.width = kColorVal;
    valBar.height = kColorWheel;
    valBar.shrink = 0.0f;
    uiColor(s).colorVal = spawn(s, wheelRowId, valBar, colorRampPart(s.look, UiToken::Content, kRampColorValue));
    uiDrag(s, uiColor(s).colorVal, colorOnDrag, &s);
    UiFlex side{};
    side.direction = UiFlexDir::Column;
    side.widthMode = UiSize::Px;
    side.width = kColorSide;
    side.heightMode = UiSize::Px;
    side.height = kColorWheel;
    side.gap = 8.0f;
    side.shrink = 0.0f;
    const uint16_t sideId = uiPanel(s, wheelRowId, side, paintClear());
    UiFlex swatch{};
    swatch.heightMode = UiSize::Px;
    swatch.height = 72.0f;
    swatch.widthMode = UiSize::Percent;
    swatch.width = 100.0f;
    swatch.shrink = 0.0f;
    {
        UiPaint swatchPaint = colorRampPart(s.look, UiToken::Preview, kRampColorSwatch, 6.0f);
        swatchPaint.r = uiColor(s).colorR;
        swatchPaint.g = uiColor(s).colorG;
        swatchPaint.b = uiColor(s).colorB;
        uiColor(s).colorSwatch = uiPanel(s, sideId, swatch, swatchPaint);
    }
    const uint16_t pickId = uiButton(s, sideId, {.label = "PICK", .height = 32.0f, .fillWidth = true, .onClick = colorOnDrop, .user = &s});
    uiColor(s).colorPickBtn = pickId;
    if (pickId < s.count) {
        if (auto* paint = s.ent[pickId].try_get_mut<UiPaint>()) {
            paint->icon = static_cast<uint8_t>(UiIcon::Search);
        }
    }
    const char* names[7] = {"R", "G", "B", "A", "H", "S", "V"};
    const float starts[7] = {uiColor(s).colorR, uiColor(s).colorG, uiColor(s).colorB, uiColor(s).colorA, uiColor(s).colorH, uiColor(s).colorS, uiColor(s).colorV};
    for (int i = 0; i < 7; ++i) {
        UiFlex line = row;
        line.heightMode = UiSize::Px;
        line.height = 26.0f;
        line.shrink = 0.0f;
        line.align = UiAlign::Center;
        line.gap = 8.0f;
        const uint16_t lineId = uiPanel(s, parent, line, paintClear());
        const uint16_t lab = uiLabel(s, lineId, names[i]);
        (void)lab;
        const uint16_t sliderId = uiSlider(s, lineId, starts[i], 0.0f, 1.0f);
        if (i < 3) {
            uiColor(s).colorRgb[i] = sliderId;
        } else if (i == 3) {
            uiColor(s).colorAlpha = sliderId;
        } else {
            uiColor(s).colorHsv[i - 4] = sliderId;
        }
        const uint16_t numId = uiField(s, lineId, "", static_cast<uint8_t>(UiFieldFilter::Digits), false);
        if (numId < s.count) {
            if (auto* flex = s.ent[numId].try_get_mut<UiFlex>()) {
                flex->widthMode = UiSize::Px;
                flex->width = kColorSide;
                flex->heightMode = UiSize::Px;
                flex->height = 24.0f;
                flex->shrink = 0.0f;
            }
            if (auto* field = s.ent[numId].try_get_mut<UiField>()) {
                field->padL = 6.0f;
                field->padR = 4.0f;
                field->maxChars = 3;
            }
            uiEdit(s, numId, colorOnEdit, &s);
        }
        uiColor(s).colorNum[i] = numId;
    }
    uiColor(s).colorHex = uiField(s, parent, "", static_cast<uint8_t>(UiFieldFilter::Any), false);
    if (uiColor(s).colorHex < s.count) {
        if (auto* flex = s.ent[uiColor(s).colorHex].try_get_mut<UiFlex>()) {
            flex->heightMode = UiSize::Px;
            flex->height = 32.0f;
            flex->shrink = 0.0f;
        }
        if (auto* hex = s.ent[uiColor(s).colorHex].try_get_mut<UiField>()) {
            hex->padL = 10.0f;
            hex->padR = 10.0f;
            hex->maxChars = 9;
        }
        uiEdit(s, uiColor(s).colorHex, colorOnEdit, &s);
    }
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
    col.direction = UiFlexDir::Column;
    col.shrink = 1.0f;
    UiFlex row = col;
    row.direction = UiFlexDir::Row;
    UiFlex rootF = col;
    rootF.pad = 12.0f;
    rootF.gap = 8.0f;
    const uint16_t root = uiPanel(s, kUiNone, rootF, paintToken(s.look, UiToken::Menu, 0.0f));
    UiFlex titleF = row;
    titleF.heightMode = UiSize::Px;
    titleF.height = 36.0f;
    titleF.shrink = 0.0f;
    titleF.align = UiAlign::Center;
    const uint16_t title = uiPanel(s, root, titleF, paintToken(s.look, UiToken::Title, 0.0f));
    if (title < s.count) {
        s.ent[title].add<UiTitleBar>();
    }
    const uint16_t brand = uiLabel(s, title, "COLOR");
    (void)brand;
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.heightMode = UiSize::Px;
    gap.height = 1.0f;
    uiPanel(s, title, gap, paintClear());
    const uint16_t close = uiButton(s, title, {.label = "", .width = 28.0f, .height = 28.0f, .onClick = colorOnClose, .user = &s});
    if (close < s.count) {
        if (auto* paint = s.ent[close].try_get_mut<UiPaint>()) {
            paint->icon = static_cast<uint8_t>(UiIcon::Dismiss);
            paint->token = UiToken::Close;
        }
    }
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
    const UiRamp ramp = paint.ramp;
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
    flex.position = UiPosition::Absolute;
    flex.posX = node.posX;
    flex.posY = node.posY;
    flex.widthMode = UiSize::Px;
    flex.width = node.width;
    flex.heightMode = UiSize::Px;
    flex.height = node.height;
    flex.pad = 10.0f;
    flex.gap = 6.0f;
    flex.shrink = 0.0f;
    const uint16_t id = uiPanel(s, parent, flex, paintRgb(node.r, node.g, node.b, node.radius));
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
    uiBinRole(s, kBinSlotColor, "color", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotColor, loadColorBin, nullptr);
    uiDrawAdd(s, colorCustomDraw);
    for (uint8_t i = 0; i < s.valueN; ++i) {
        if (s.valueFn[i] == colorOnValue) {
            return;
        }
    }
    uiListen(s, colorOnValue, &s);
}

} // namespace burnhope

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
#include "ui/Bar.hpp"
#include "ui/Primitive.hpp"
#include "ui/PrimitiveBatch.hpp"
#include "ui/widget/Slider.hpp"
#include "color/Color.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>
#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {
namespace {
constexpr UiRamp kRampColorWheel = UiRamp::HueWheel;
constexpr UiRamp kRampColorValue = UiRamp::ValueRamp;
constexpr UiRamp kRampColorSwatch = UiRamp::Gradient;
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

void publishColor(UiState& s, UiColorPickerComponent& picker);
void refreshColorChrome(UiState& s, UiColorPickerComponent& picker);
void fillColorBody(UiState& s, uint16_t parent);
void colorOnClose(void* user, uint16_t);
void takeBook(UiColorPickerComponent& picker);
void applyColor(UiState& s, UiColorPickerComponent& picker, bool fromHsv, bool publish);

bool pickerOwns(const UiColorPickerComponent& picker, uint16_t id) {
    if (id == kUiNone) {
        return false;
    }
    if (id == picker.colorPanel || id == picker.colorSwatch || id == picker.colorTarget || id == picker.colorWheel
        || id == picker.colorVal || id == picker.colorAlpha || id == picker.colorHex || id == picker.colorAnchor
        || id == picker.colorAlphaBtn || id == picker.colorMenuBtn || id == picker.colorWinBtn || id == picker.colorPickBtn) {
        return true;
    }
    for (uint16_t slot : picker.colorRgb) {
        if (slot == id) {
            return true;
        }
    }
    for (uint16_t slot : picker.colorHsv) {
        if (slot == id) {
            return true;
        }
    }
    for (uint16_t slot : picker.colorNum) {
        if (slot == id) {
            return true;
        }
    }
    return false;
}

UiColorPickerComponent* pickerFor(UiState& s, uint16_t id) {
    if (id < s.count) {
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].is_alive() && s.ent[p].has<UiColorPickerComponent>()) {
                return s.ent[p].try_get_mut<UiColorPickerComponent>();
            }
        }
        const uint16_t owner = uiColorFirst(s, [id](const UiColorPickerComponent& picker) { return pickerOwns(picker, id); });
        if (owner != kUiNone) {
            return s.ent[owner].try_get_mut<UiColorPickerComponent>();
        }
    }
    return uiColorFind(s);
}

const UiColorPickerComponent* pickerFor(const UiState& s, uint16_t id) {
    if (id < s.count) {
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].is_alive() && s.ent[p].has<UiColorPickerComponent>()) {
                return s.ent[p].try_get<UiColorPickerComponent>();
            }
        }
        const uint16_t owner = uiColorFirst(s, [id](const UiColorPickerComponent& picker) { return pickerOwns(picker, id); });
        if (owner != kUiNone) {
            return s.ent[owner].try_get<UiColorPickerComponent>();
        }
    }
    return uiColorFind(s);
}

UiColorPickerComponent* hostPicker(UiState& s) {
    const uint16_t host = uiColorFirst(s, [](const UiColorPickerComponent& picker) { return picker.colorHost == 0; });
    if (host != kUiNone) {
        return s.ent[host].try_get_mut<UiColorPickerComponent>();
    }
    return uiColorFind(s);
}

UiColorPickerComponent& colorBind(UiState& s, uint16_t panel) {
    if (panel < s.count && s.ent[panel].has<UiColorPickerComponent>()) {
        return *s.ent[panel].try_get_mut<UiColorPickerComponent>();
    }
    UiColorPickerComponent born{};
    if (const UiColorPickerComponent* prior = uiColorFind(s)) {
        born.book = prior->book;
        born.onColor = prior->onColor;
        born.colorUser = prior->colorUser;
    }
    s.ent[panel].set<UiColorPickerComponent>(born);
    return *s.ent[panel].try_get_mut<UiColorPickerComponent>();
}

void uiSetColorBook(UiState& s, ColorBook* book) {
    uiColorEach(s, [&](uint16_t id) {
        UiColorPickerComponent* picker = s.ent[id].try_get_mut<UiColorPickerComponent>();
        if (picker == nullptr) {
            return;
        }
        picker->book = book;
        if (book != nullptr && book->gen != 0 && picker->colorSeen != book->gen) {
            takeBook(*picker);
            applyColor(s, *picker, false, true);
        }
    });
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

void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a) {
    const auto byte = [](float c) { return static_cast<unsigned>(std::clamp(c * 255.0f + 0.5f, 0.0f, 255.0f)); };
    std::format_to_n(dst, 9, "#{:02X}{:02X}{:02X}{:02X}", byte(r), byte(g), byte(b), byte(a));
    dst[9] = '\0';
    len = 9;
}

void applyColor(UiState& s, UiColorPickerComponent& picker, bool fromHsv, bool publish = true) {
    if (fromHsv) {
        ColorLinear linear{};
        colorHsvToLinear(colorInk(picker).h, colorInk(picker).s, colorInk(picker).v, linear);
        colorInk(picker).r = linear.r;
        colorInk(picker).g = linear.g;
        colorInk(picker).b = linear.b;
    } else {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
    }
    setRange(s, picker.colorRgb[0], colorInk(picker).r);
    setRange(s, picker.colorRgb[1], colorInk(picker).g);
    setRange(s, picker.colorRgb[2], colorInk(picker).b);
    setRange(s, picker.colorHsv[0], colorInk(picker).h);
    setRange(s, picker.colorHsv[1], colorInk(picker).s);
    setRange(s, picker.colorHsv[2], colorInk(picker).v);
    for (uint16_t slot : {picker.colorRgb[0], picker.colorRgb[1], picker.colorRgb[2], picker.colorAlpha, picker.colorHsv[0], picker.colorHsv[1], picker.colorHsv[2]}) {
        if (slot == kUiNone || slot >= s.count) {
            continue;
        }
        auto* ramp = s.ent[slot].try_get_mut<UiPaint>();
        if (ramp != nullptr) {
            ramp->r = colorInk(picker).r;
            ramp->g = colorInk(picker).g;
            ramp->b = colorInk(picker).b;
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
        writeHex(field->bytes, field->len, colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).a);
        field->caret = field->len;
        field->anchor = field->len;
        fieldSyncText(s, id);
    };
    write(picker.colorTarget, false);
    write(picker.colorHex, true);
    const int shown[7] = {
        static_cast<int>(colorInk(picker).r * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).g * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).b * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).a * 255.0f + 0.5f),
        static_cast<int>(colorInk(picker).h * 360.0f + 0.5f),
        static_cast<int>(colorInk(picker).s * 100.0f + 0.5f),
        static_cast<int>(colorInk(picker).v * 100.0f + 0.5f),
    };
    for (int i = 0; i < 7; ++i) {
        const uint16_t id = picker.colorNum[i];
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
    setRange(s, picker.colorAlpha, colorInk(picker).a);
    if (publish) {
        publishColor(s, picker);
    }
    refreshColorChrome(s, picker);
    s.visualDirty = true;
}

void takeBook(UiColorPickerComponent& picker) {
    if (picker.book == nullptr || picker.book->gen == 0) {
        return;
    }
    picker.colorSeen = picker.book->gen;
    picker.colorApplied = picker.book->present;
}

void paintColor(UiState& s) {
    if (UiColorPickerComponent* picker = uiColorFind(s)) {
        applyColor(s, *picker, false);
    }
}

void dragWheel(UiState& s, UiColorPickerComponent& picker, uint16_t id, float x, float y) {
    const UiBox& b = s.box[id];
    if (id == picker.colorWheel) {
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
        colorInk(picker).h = hue;
        colorInk(picker).s = sat;
        applyColor(s, picker, true);
    } else if (id == picker.colorVal) {
        float t = b.h > 1.0f ? 1.0f - (y - b.y) / b.h : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        colorInk(picker).v = t;
        applyColor(s, picker, true);
    } else if (id == picker.colorAlpha) {
        float t = b.w > 1.0f ? (x - b.x) / b.w : 0.0f;
        if (t < 0.0f) {
            t = 0.0f;
        }
        if (t > 1.0f) {
            t = 1.0f;
        }
        colorInk(picker).a = t;
        applyColor(s, picker, true);
    }
}

bool readHex(const UiField& field, float& r, float& g, float& b, float& a) {
    std::string_view digits(field.bytes, field.len);
    if (digits.starts_with('#')) {
        digits.remove_prefix(1);
    }
    if (digits.size() != 6 && digits.size() != 8) {
        return false;
    }
    uint32_t packed = 0;
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), packed, 16);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return false;
    }
    const bool withAlpha = digits.size() == 8;
    const uint32_t rgb = withAlpha ? packed >> 8 : packed;
    r = static_cast<float>((rgb >> 16) & 255u) / 255.0f;
    g = static_cast<float>((rgb >> 8) & 255u) / 255.0f;
    b = static_cast<float>(rgb & 255u) / 255.0f;
    if (withAlpha) {
        a = static_cast<float>(packed & 255u) / 255.0f;
    }
    return true;
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
                ColorLinear linear{};
                colorHsvToLinear(hue, sat, 1.0f, linear);
                r = linear.r;
                g = linear.g;
                b = linear.b;
                a = 1.0f;
                return true;
            }
            if (paint->ramp == kRampColorSwatch) {
                const UiColorPickerComponent* owned = pickerFor(s, id);
                if (owned == nullptr) {
                    continue;
                }
                r = colorInk(*owned).r;
                g = colorInk(*owned).g;
                b = colorInk(*owned).b;
                a = colorInk(*owned).a;
                return true;
            }
            if (paint->ramp == kRampColorValue) {
                const UiColorPickerComponent* owned = pickerFor(s, id);
                if (owned == nullptr) {
                    continue;
                }
                float v = box.h > 1.0f ? 1.0f - (y - box.y) / box.h : 1.0f;
                if (v < 0.0f) {
                    v = 0.0f;
                }
                if (v > 1.0f) {
                    v = 1.0f;
                }
                ColorLinear linear{};
                colorHsvToLinear(colorInk(*owned).h, colorInk(*owned).s, v, linear);
                r = linear.r;
                g = linear.g;
                b = linear.b;
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

void publishColor(UiState& s, UiColorPickerComponent& picker) {
    if (picker.book == nullptr) {
        return;
    }
    ColorBook& b = colorInk(picker);
    ++b.gen;
    picker.colorSeen = b.gen;
    if (picker.onColor != nullptr && b.present != 0 && picker.colorAnchor != kUiNone) {
        picker.onColor(picker.colorUser, s, picker.colorAnchor, b.r, b.g, b.b);
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

void refreshColorChrome(UiState& s, UiColorPickerComponent& picker) {
    setBtnLabel(s, picker.colorMenuBtn, colorInk(picker).mode == 0 ? "MENU *" : "MENU");
    setBtnLabel(s, picker.colorWinBtn, colorInk(picker).mode == 1 ? "WINDOW *" : "WINDOW");
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

void placeColorMenu(UiState& s, UiColorPickerComponent& picker) {
    float x = 24.0f;
    float y = 48.0f;
    const float w = kColorW;
    const float h = kColorH;
    if (picker.colorAnchor != kUiNone && picker.colorAnchor < s.count) {
        const UiBox& b = s.box[picker.colorAnchor];
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
    uiPlace(s, picker.colorPanel, true, x, y, w, h);
}

void syncColor(UiState& s, UiColorPickerComponent& picker) {
    if (picker.book == nullptr) {
        return;
    }
    const ColorBook& b = *picker.book;
    if (b.gen != picker.colorSeen) {
        picker.colorSeen = b.gen;
        applyColor(s, picker, false, false);
    }
    if (colorInk(picker).present == picker.colorApplied) {
        return;
    }
    picker.colorApplied = colorInk(picker).present;
    if (picker.colorHost == 0) {
        if (colorInk(picker).present == 1) {
            placeColorMenu(s, picker);
            uiShow(s, picker.colorPanel, true);
        } else {
            uiShow(s, picker.colorPanel, false);
            if (colorInk(picker).present == 2 && picker.book != nullptr) {
                picker.book->present = 2;
            }
        }
    } else if (colorInk(picker).present != 2) {
        uiRequestClose(s);
    }
}

void uiEnsureColorPopup(UiState& s) {
    const uint16_t live = uiColorFirst(s, [&s](const UiColorPickerComponent& picker) {
        return picker.colorHost == 0 && picker.colorPanel < s.count && s.ent[picker.colorPanel].is_alive();
    });
    if (live != kUiNone) {
        return;
    }
    if (s.count == 0 || s.yoga[0] == nullptr) {
        return;
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
    UiColorPickerComponent& picker = colorBind(s, id);
    picker.colorHost = 0;
    if (picker.colorSeen == 0) {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
    }
    picker.colorPanel = id;
    colorInk(picker).present = 0;
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
    UiColorPickerComponent* picker = hostPicker(s);
    if (picker == nullptr) {
        return;
    }
    if (picker->book != nullptr && picker->book->gen != picker->colorSeen) {
        takeBook(*picker);
    }
    picker->colorAnchor = id;
    applyColor(s, *picker, false, false);
    if (colorInk(*picker).mode == 1) {
        colorInk(*picker).present = 2;
        picker->colorApplied = 2;
        uiShow(s, picker->colorPanel, false);
        if (picker->book != nullptr) {
            picker->book->present = 2;
        }
    } else {
        colorInk(*picker).present = 1;
        picker->colorApplied = 1;
        placeColorMenu(s, *picker);
        uiShow(s, picker->colorPanel, true);
    }
    publishColor(s, *picker);
}

void colorOnClose(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    colorInk((*picker)).present = 0;
    (*picker).colorApplied = 0;
    uiShow(s, (*picker).colorPanel, false);
    publishColor(s, *picker);
    if ((*picker).colorHost != 0) {
        uiRequestClose(s);
    }
}

void colorOnDrop(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    colorInk((*picker)).drop = colorInk((*picker)).drop == 0 ? 1 : 0;
    publishColor(s, *picker);
    if ((*picker).colorHost == 0 && colorInk((*picker)).present == 1) {
        if (colorInk((*picker)).drop == 0) {
            placeColorMenu(s, *picker);
        }
        uiShow(s, (*picker).colorPanel, colorInk((*picker)).drop == 0);
    }
}

void colorOnMenu(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    colorInk((*picker)).mode = 0;
    colorInk((*picker)).present = 1;
    (*picker).colorApplied = 1;
    publishColor(s, *picker);
    refreshColorChrome(s, *picker);
    if ((*picker).colorHost != 0) {
        uiRequestClose(s);
    } else {
        placeColorMenu(s, *picker);
        uiShow(s, (*picker).colorPanel, true);
    }
}

void colorOnWindow(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiColorPickerComponent* picker = pickerFor(s, id);
    if (picker == nullptr) {
        return;
    }
    colorInk((*picker)).mode = 1;
    colorInk((*picker)).present = 2;
    (*picker).colorApplied = 2;
    uiShow(s, (*picker).colorPanel, false);
    publishColor(s, *picker);
    refreshColorChrome(s, *picker);
    if ((*picker).colorHost == 0 && (*picker).book != nullptr) {
        (*picker).book->present = 2;
    }
}

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

void fillColorBody(UiState& s, uint16_t parent) {
    UiColorPickerComponent* picker = pickerFor(s, parent);
    if (picker == nullptr) {
        return;
    }

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
    (*picker).colorWheel = uiPanel(s, wheelRowId, wheel, colorRampPart(s.look, UiToken::Content, kRampColorWheel));
    uiDrag(s, (*picker).colorWheel, colorOnDrag, &s);
    UiFlex valBar{};
    valBar.widthMode = UiSize::Px;
    valBar.heightMode = UiSize::Px;
    valBar.width = kColorVal;
    valBar.height = kColorWheel;
    valBar.shrink = 0.0f;
    (*picker).colorVal = uiPanel(s, wheelRowId, valBar, colorRampPart(s.look, UiToken::Content, kRampColorValue));
    uiDrag(s, (*picker).colorVal, colorOnDrag, &s);
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
        (*picker).colorSwatch = uiPanel(s, sideId, swatch, swatchPaint);
    }
    const uint16_t pickId = uiButton(s, sideId, {.label = "PICK", .height = 32.0f, .fillWidth = true, .onClick = colorOnDrop, .user = &s});
    (*picker).colorPickBtn = pickId;
    if (pickId < s.count) {
        if (auto* paint = s.ent[pickId].try_get_mut<UiPaint>()) {
            paint->icon = static_cast<uint8_t>(UiIcon::Search);
        }
    }
    const char* names[7] = {"R", "G", "B", "A", "H", "S", "V"};
    const float starts[7] = {colorInk((*picker)).r, colorInk((*picker)).g, colorInk((*picker)).b, colorInk((*picker)).a, colorInk((*picker)).h, colorInk((*picker)).s, colorInk((*picker)).v};
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
        const UiValueChangeFn slide[7] = {slideR, slideG, slideB, slideA, slideH, slideS, slideV};
        const uint16_t sliderId = uiSlider(s, lineId, starts[i], 0.0f, 1.0f, slide[i], &s);
        if (i < 3) {
            (*picker).colorRgb[i] = sliderId;
        } else if (i == 3) {
            (*picker).colorAlpha = sliderId;
        } else {
            (*picker).colorHsv[i - 4] = sliderId;
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
        (*picker).colorNum[i] = numId;
    }
    (*picker).colorHex = uiField(s, parent, "", static_cast<uint8_t>(UiFieldFilter::Any), false);
    if ((*picker).colorHex < s.count) {
        if (auto* flex = s.ent[(*picker).colorHex].try_get_mut<UiFlex>()) {
            flex->heightMode = UiSize::Px;
            flex->height = 32.0f;
            flex->shrink = 0.0f;
        }
        if (auto* hex = s.ent[(*picker).colorHex].try_get_mut<UiField>()) {
            hex->padL = 10.0f;
            hex->padR = 10.0f;
            hex->maxChars = 9;
        }
        uiEdit(s, (*picker).colorHex, colorOnEdit, &s);
    }
    s.grabFn = colorGrab;
    s.grabUser = &s;
    applyColor(s, *picker, true, (*picker).colorSeen == 0);
}

void uiBuildColor(UiState& s) {
    UiFlex col{};
    col.direction = UiFlexDir::Column;
    col.shrink = 1.0f;
    UiFlex row = col;
    row.direction = UiFlexDir::Row;
    UiFlex rootF = col;
    rootF.pad = 12.0f;
    rootF.gap = 8.0f;
    const uint16_t root = uiPanel(s, kUiNone, rootF, paintToken(s.look, UiToken::Menu, 0.0f));
    if (root == kUiNone) {
        return;
    }
    UiColorPickerComponent& picker = colorBind(s, root);
    picker.colorHost = 1;
    if (picker.book != nullptr && picker.book->gen != 0) {
        takeBook(picker);
    } else {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
    }
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
    picker.colorPanel = root;
    colorInk(picker).present = 2;
    picker.colorApplied = 2;
    colorInk(picker).mode = 1;
    fillColorBody(s, root);
}

uint16_t loadColorBin(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char* name, const char*, const char*, void*) {
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
    if (id == kUiNone) {
        return kUiNone;
    }
    UiColorPickerComponent& picker = colorBind(s, id);
    if (picker.book == nullptr || picker.book->gen == 0) {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
        colorInk(picker).a = 1.0f;
        colorInk(picker).useAlpha = 1;
    } else {
        takeBook(picker);
    }
    picker.colorPanel = id;
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
    uiPlaceAdd(s, colorPlace);
}

} // namespace burnhope

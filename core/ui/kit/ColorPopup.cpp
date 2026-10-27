#include "ui/Panel.hpp"
#include "color/Color.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"
#include "ui/kit/ColorInternal.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <string_view>
#include <cmath>
#include <cstring>
#include <ctime>


namespace burnhope {

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

void takeBook(UiColorPickerComponent& picker) {
    if (picker.book == nullptr || picker.book->gen == 0) {
        return;
    }
    picker.colorSeen = picker.book->gen;
    picker.colorApplied = picker.book->present;
}

void samplePixel(const UiState& s, float x, float y, float& r, float& g, float& b, float& a) {
    tokenRgb(s.look, UiToken::Window, r, g, b);
    a = 1.0f;
    auto take = [&](bool overlay) -> bool {
        uint16_t painted[kUiCap];
        const uint16_t paintedCount = uiCollect<UiPaint>(s, painted, kUiCap);
        for (int n = static_cast<int>(paintedCount) - 1; n >= 0; --n) {
            const uint16_t id = painted[n];
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
            if (paint->a <= kUiInvisibleAlpha) {
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
    const uint16_t id = uiSpawnTemplate(s, 0, "core/panel", "color_popup");
    if (id == kUiNone) {
        return;
    }
    uiPlace(s, id, true, 0.0f, 0.0f, 0.0f, 0.0f);
    UiColorPickerComponent& picker = colorBind(s, id);
    picker.colorHost = 0;
    if (picker.colorSeen == 0) {
        colorLinearToHsv(colorInk(picker).r, colorInk(picker).g, colorInk(picker).b, colorInk(picker).h, colorInk(picker).s, colorInk(picker).v);
    }
    picker.colorPanel = id;
    colorInk(picker).present = 0;
    picker.colorApplied = 0;
    colorHead(s, id, "Color");
    const uint16_t bodyId = uiSpawnTemplate(s, id, "core/panel", "color_body");
    uiSetGrow(s, bodyId, 1.0f, 1.0f); // the body takes what the title leaves
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

} // namespace burnhope

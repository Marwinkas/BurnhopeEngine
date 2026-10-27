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

namespace burnhope {

static bool pickerOwns(const UiColorPickerComponent& picker, uint16_t id) {
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

static void setBtnLabel(UiState& s, uint16_t id, const char* text) {
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

void placeColorMenu(UiState& s, UiColorPickerComponent& picker) {
    float x = 24.0f;
    float y = 48.0f;
    const float w = uiStyle(s, "color_popup", UiStyleKey::Width);
    const float h = uiStyle(s, "color_popup", UiStyleKey::Height);
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

void uiBuildColor(UiState& s) {
    const uint16_t root = uiSpawnTemplate(s, kUiNone, "core/column", "color_flat");
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
    colorHead(s, root, "COLOR", "cw_title_flat");
    picker.colorPanel = root;
    colorInk(picker).present = 2;
    picker.colorApplied = 2;
    colorInk(picker).mode = 1;
    fillColorBody(s, root);
}

uint16_t loadColorBin(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char* name, const char*, const char*, void*) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/column", "color_flat");
    if (id == kUiNone) {
        return kUiNone;
    }
    uiPlace(s, id, true, node.posX, node.posY, node.width, node.height);
    s.ent[id].set<UiPaint>(paintRgb(node.r, node.g, node.b, node.radius));
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

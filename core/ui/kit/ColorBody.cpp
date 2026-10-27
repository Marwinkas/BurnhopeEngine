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

void colorHead(UiState& s, uint16_t parent, const char* label, const char* cls) {
    const UiTemplateId tpl = uiTemplateFind(s, "core/color_head");
    const uint16_t title = uiSpawnTemplate(s, parent, tpl, cls);
    if (title >= s.count) {
        return;
    }
    uiText(s, uiTemplateSlot(s, tpl, title, "brand"), label);
    const uint16_t close = uiTemplateSlot(s, tpl, title, "close");
    uiSetIcon(s, close, UiIcon::Dismiss);
    uiBind(s, close, colorOnClose, &s);
}

void fillColorBody(UiState& s, uint16_t parent) {
    UiColorPickerComponent* picker = pickerFor(s, parent);
    if (picker == nullptr) {
        return;
    }
    const UiTemplateId rowTpl = uiTemplateFind(s, "core/color_wheel_row");
    const uint16_t wheelRowId = uiSpawnTemplate(s, parent, rowTpl);
    if (wheelRowId == kUiNone) {
        return;
    }
    (*picker).colorWheel = uiTemplateSlot(s, rowTpl, wheelRowId, "wheel");
    uiDrag(s, (*picker).colorWheel, colorOnDrag, &s);
    (*picker).colorVal = uiTemplateSlot(s, rowTpl, wheelRowId, "val");
    uiDrag(s, (*picker).colorVal, colorOnDrag, &s);
    (*picker).colorSwatch = uiTemplateSlot(s, rowTpl, wheelRowId, "swatch");
    const uint16_t pickId = uiTemplateSlot(s, rowTpl, wheelRowId, "pick");
    (*picker).colorPickBtn = pickId;
    uiText(s, pickId, "PICK");
    uiSetIcon(s, pickId, UiIcon::Search);
    uiBind(s, pickId, colorOnDrop, &s);
    const char* names[7] = {"R", "G", "B", "A", "H", "S", "V"};
    const float starts[7] = {colorInk((*picker)).r, colorInk((*picker)).g, colorInk((*picker)).b, colorInk((*picker)).a, colorInk((*picker)).h, colorInk((*picker)).s, colorInk((*picker)).v};
    const UiValueChangeFn slide[7] = {slideR, slideG, slideB, slideA, slideH, slideS, slideV};
    const UiTemplateId lineTpl = uiTemplateFind(s, "core/color_line");
    for (int i = 0; i < 7; ++i) {
        const uint16_t lineId = uiSpawnTemplate(s, parent, lineTpl);
        if (lineId == kUiNone) {
            return;
        }
        uiText(s, uiTemplateSlot(s, lineTpl, lineId, "label"), names[i]);
        const uint16_t sliderId = uiTemplateSlot(s, lineTpl, lineId, "slider");
        uiSliderInit(s, sliderId, starts[i], 0.0f, 1.0f, slide[i], &s);
        if (i < 3) {
            (*picker).colorRgb[i] = sliderId;
        } else if (i == 3) {
            (*picker).colorAlpha = sliderId;
        } else {
            (*picker).colorHsv[i - 4] = sliderId;
        }
        const uint16_t numId = uiTemplateSlot(s, lineTpl, lineId, "num");
        uiBindField(s, numId, "", static_cast<uint8_t>(UiFieldFilter::Digits), false);
        if (auto* field = s.ent[numId].try_get_mut<UiField>()) {
            field->padL = 6.0f;
            field->padR = 4.0f;
            field->maxChars = 3;
        }
        uiEdit(s, numId, colorOnEdit, &s);
        (*picker).colorNum[i] = numId;
    }
    (*picker).colorHex = uiSpawnTemplate(s, parent, "core/color_hex");
    if ((*picker).colorHex < s.count) {
        // The hex field runs the width of the picker body.
        uiSetWidthPercent(s, (*picker).colorHex, 100.0f);
        uiBindField(s, (*picker).colorHex, "", static_cast<uint8_t>(UiFieldFilter::Any), false);
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

} // namespace burnhope

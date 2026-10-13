#include "ui/widget/Widgets.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/Layout.hpp"

namespace burnhope {
namespace {

void hostText(UiState& s, uint16_t id, uint8_t align, uint8_t ellipsis) {
    if (s.ent[id].try_get<UiText>() != nullptr) {
        return;
    }
    UiText host{};
    host.align = align;
    host.ellipsis = ellipsis;
    host.valign = 1;
    s.ent[id].set<UiText>(host);
}

void adornButton(UiState& s, uint16_t id) {
    hostText(s, id, 1, 1);
}

void adornClose(UiState& s, uint16_t id) {
    hostText(s, id, 1, 1);
}

void adornField(UiState& s, uint16_t id) {
    hostText(s, id, 0, 0);
}

void adornCheck(UiState& s, uint16_t id) {
    hostText(s, id, 0, 1);
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        if (flex->padL < 32.0f) {
            flex->padL = 32.0f;
        }
    }
}

void finishField(UiState& s, uint16_t id, const BinNode* spec, const char* text, const char* hint) {
    hostText(s, id, 0, 0);
    uint8_t flags = 0;
    uint8_t filter = 0;
    uint8_t maxChars = 0;
    float padL = 12.0f;
    float padR = 12.0f;
    if (spec != nullptr) {
        flags = static_cast<uint8_t>(spec->flags & (kBinClear | kBinRequired | kBinReadOnly | kBinPassword | kBinForm));
        filter = spec->filter;
        maxChars = spec->maxChars;
        if (spec->padL > 0.0f) {
            padL = spec->padL;
        }
        if (spec->padR > 0.0f) {
            padR = spec->padR;
        }
    }
    uiFieldInit(s, id, text, hint, filter, flags, maxChars, padL, padR);
}

void finishCheck(UiState& s, uint16_t id, const BinNode* spec, const char*, const char*) {
    UiCheck check{};
    if (spec != nullptr) {
        check.on = (spec->flags & kBinCheckOn) != 0 ? 1 : 0;
        check.kind = spec->checkKind;
        check.group = spec->checkGroup;
    }
    s.ent[id].set<UiCheck>(check);
}

void finishRange(UiState& s, uint16_t id, const BinNode* spec, const char*, const char*) {
    if (spec == nullptr) {
        s.ent[id].set<UiRange>({});
        return;
    }
    s.ent[id].set<UiRange>({spec->value, spec->minV, spec->maxV});
}

} // namespace

void uiWidgetsInit(UiState& s) {
    uiRegisterSpawnHook(s, static_cast<uint8_t>(UiRole::Button), adornButton);
    uiRegisterSpawnHook(s, static_cast<uint8_t>(UiRole::Close), adornClose);
    uiRegisterSpawnHook(s, static_cast<uint8_t>(UiRole::Field), adornField);
    uiRegisterSpawnHook(s, static_cast<uint8_t>(UiRole::Check), adornCheck);
    uiRegisterRoleFinish(s, static_cast<uint8_t>(UiRole::Field), finishField);
    uiRegisterRoleFinish(s, static_cast<uint8_t>(UiRole::Check), finishCheck);
    uiRegisterRoleFinish(s, static_cast<uint8_t>(UiRole::Slider), finishRange);
    uiRegisterRoleFinish(s, static_cast<uint8_t>(UiRole::Progress), finishRange);
    uiInstallChromeBin(s);
    uiInstallDockOverlay(s);
    uiDrawDecorAdd(s, uiFieldDecor);
    uiDrawDecorAdd(s, uiCurveDecor);
    uiOverlayAdd(s, uiScrollOverlay);
    uiInputAdd(s, uiSplitInput);
    uiInputAdd(s, uiFieldInput);
    uiInputAdd(s, uiRubberInput);
    uiInputAdd(s, uiMenuInput);
    uiInputAdd(s, uiDockInput);
    uiInputAdd(s, uiScrollInput);
    uiInputAdd(s, uiVirtualGridInput);
    uiInputAdd(s, uiSliderInput);
}

namespace {

struct WidgetsBoot {
    WidgetsBoot() {
        uiBootAdd(uiWidgetsInit);
    }
};

const WidgetsBoot gWidgetsBoot{};

} // namespace

} // namespace burnhope

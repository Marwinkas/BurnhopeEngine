#include "ui/widget/Button.hpp"
#include "ui/widget/Widgets.hpp"
#include "ui/UiBin.hpp"
#include "ui/Hover.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/LayoutBridge.hpp"

namespace burnhope {
namespace {

void hostText(UiState& s, uint16_t id, UiTextAlign align, UiTextEllipsis ellipsis) {
    if (s.ent[id].try_get<UiText>() != nullptr) {
        return;
    }
    UiText host{};
    host.align = align;
    host.ellipsis = ellipsis;
    host.valign = UiTextValign::Center;
    s.ent[id].set<UiText>(host);
}

void adornButton(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Center, UiTextEllipsis::End);
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void adornSlider(UiState& s, uint16_t id) {
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void adornClose(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Center, UiTextEllipsis::End);
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
}

void adornField(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Left, UiTextEllipsis::None);
}

void adornCheck(UiState& s, uint16_t id) {
    hostText(s, id, UiTextAlign::Left, UiTextEllipsis::End);
    if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
        if (flex->padL < 32.0f) {
            flex->padL = 32.0f;
        }
    }
}

void finishField(UiState& s, uint16_t id, const BinNode* spec, const char* text, const char* hint) {
    hostText(s, id, UiTextAlign::Left, UiTextEllipsis::None);
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
    uint8_t on = 0;
    uint8_t kind = 0;
    uint8_t group = 0;
    if (spec != nullptr) {
        on = (spec->flags & kBinCheckOn) != 0 ? 1 : 0;
        kind = spec->checkKind;
        group = spec->checkGroup;
    }
    uiHover(s, id);
    s.ent[id].add<UiFocusable>();
    uiBindCheck(s, id, kind, group, on != 0);
}

void scrollBindInner(UiState& s, uint16_t id, void*) {
    auto* scroll = id < s.count ? s.ent[id].try_get_mut<UiScroll>() : nullptr;
    if (scroll == nullptr || scroll->inner != kUiNone) {
        return;
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == id) {
            scroll->inner = child;
            return;
        }
    }
}

void finishScroll(UiState& s, uint16_t id, const BinNode*, const char*, const char*) {
    s.ent[id].set<UiScroll>({0.0f, kUiNone});
    s.ent[id].set<UiLayoutRefresh>({scrollBindInner, nullptr});
}

void finishRange(UiState& s, uint16_t id, const BinNode* spec, const char*, const char*) {
    if (spec == nullptr) {
        s.ent[id].set<UiRange>({});
        return;
    }
    s.ent[id].set<UiRange>({spec->value, spec->minV, spec->maxV});
}

uint16_t loadSplit(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    const bool vertical = node.direction != static_cast<uint8_t>(UiFlexDir::Row);
    return uiSplitBar(s, parent, vertical);
}

uint16_t loadTree(UiState& s, uint16_t parent, const BinNode&, const char*, void*) {
    return uiTreeView(s, parent, {});
}

uint16_t loadGrid(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    const float itemW = node.width > 1.0f ? node.width : 84.0f;
    const float itemH = node.height > 1.0f ? node.height : itemW;
    return uiVirtualGrid(s, parent, {.itemW = itemW, .itemH = itemH});
}

uint16_t loadCrumbs(UiState& s, uint16_t parent, const BinNode&, const char*, void*) {
    return uiBreadcrumbs(s, parent, nullptr, nullptr);
}

uint16_t loadChoice(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    if (s.importBag.count == 0) {
        return kUiNone;
    }
    const char* labels[kUiImportItemCap]{};
    for (uint8_t i = 0; i < s.importBag.count; ++i) {
        labels[i] = s.importBag.item[i];
    }
    uint8_t selected = static_cast<uint8_t>(node.value);
    if (selected >= s.importBag.count) {
        selected = 0;
    }
    return uiChoice(s, parent, {
        .labels = labels,
        .count = s.importBag.count,
        .selected = selected,
        .width = node.width > 48.0f ? node.width : 140.0f,
    });
}

uint16_t loadSearch(UiState& s, uint16_t parent, const BinNode& node, const char*, void*) {
    const float width = node.width > 1.0f ? node.width : 180.0f;
    const char* hint = s.importBag.hintLen > 0 ? s.importBag.hint : nullptr;
    return uiSearchField(s, parent, hint, nullptr, nullptr, width);
}

} // namespace

void uiWidgetsInit(UiState& s) {
    uiRegisterSpawnHook(s, kBinSlotButton, adornButton);
    uiRegisterSpawnHook(s, kBinSlotSlider, adornSlider);
    uiRegisterCustomBinNode(s, kBinSlotSplit, loadSplit, nullptr);
    uiRegisterCustomBinNode(s, kBinSlotTree, loadTree, nullptr);
    uiRegisterCustomBinNode(s, kBinSlotGrid, loadGrid, nullptr);
    uiRegisterCustomBinNode(s, kBinSlotCrumbs, loadCrumbs, nullptr);
    uiRegisterCustomBinNode(s, kBinSlotChoice, loadChoice, nullptr);
    uiRegisterCustomBinNode(s, kBinSlotSearch, loadSearch, nullptr);
    uiRegisterSpawnHook(s, kBinSlotClose, adornClose);
    uiRegisterSpawnHook(s, kBinSlotField, adornField);
    uiRegisterSpawnHook(s, kBinSlotCheck, adornCheck);
    uiRegisterRoleFinish(s, kBinSlotField, finishField);
    uiRegisterRoleFinish(s, kBinSlotCheck, finishCheck);
    uiRegisterRoleFinish(s, kBinSlotSlider, finishRange);
    uiRegisterRoleFinish(s, kBinSlotProgress, finishRange);
    uiRegisterRoleFinish(s, kBinSlotScroll, finishScroll);
    uiInstallChromeBin(s);
    uiInstallDockOverlay(s);
    uiDrawDecorAdd(s, uiFieldDecor);
    uiDrawDecorAdd(s, uiCurveDecor);
    uiDrawDecorAdd(s, uiSliderDecor);
    uiDrawDecorAdd(s, uiCheckDecor);
    uiOverlayAdd(s, uiScrollOverlay);
    uiInputAdd(s, uiSplitInput);
    uiInputAdd(s, uiFieldInput);
    uiInputAdd(s, uiRubberInput);
    uiInputAdd(s, uiMenuInput);
    uiInputAdd(s, uiDockInput);
    uiInputAdd(s, uiScrollInput);
    uiInputAdd(s, uiVirtualGridInput);
    uiInputAdd(s, uiTreeInput);
    uiInputAdd(s, uiDragDropInput);
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

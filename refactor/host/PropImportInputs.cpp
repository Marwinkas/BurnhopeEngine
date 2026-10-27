#include "host/PropImportInternal.hpp"
#include "Str.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/State.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope::propimport {
uint16_t makeChoice(Import& in, uint16_t parent, const Node& node) {
    const char* labels[8]{};
    for (uint8_t i = 0; i < node.itemN && i < 8; ++i) {
        labels[i] = node.items[i].label;
    }
    const float width = node.w > 0.0f ? node.w : uiChoiceWidth(*in.state, labels, node.itemN);
    UiCommandPick* pick = takeCommand(in, node.command, 0);
    const uint16_t id = uiChoice(*in.state, parent, {
        .labels = labels,
        .count = node.itemN,
        .selected = static_cast<uint8_t>(node.value),
        .onPick = pick != nullptr ? onChoiceCommand : nullptr,
        .user = pick,
    });
    uiChoiceFit(*in.state, id, width);
    remember(in, node.name, id);
    return id;
}

uint16_t makeNumber(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiNumber(*in.state, parent, node.value, node.minV, node.maxV, node.hasStep ? node.step : 0.01f);
    uiSetHeight(*in.state, id, node.h > 0.0f ? node.h : uiStyle(*in.state, "number_field", UiStyleKey::Height));
    if (node.w > 0.0f) {
        uiSetWidth(*in.state, id, node.w);
    }
    uiNumberFill(*in.state, id, false);
    uiApplyLayout(*in.state, id, {.w = node.w > 0.0f ? node.w : uiStyle(*in.state, "number_field", UiStyleKey::Width), .shrink = 0.0f});
    remember(in, node.name, id);
    return id;
}

uint16_t makeRange(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiSlider(*in.state, parent, node.value, node.minV, node.maxV);
    uiApplyLayout(*in.state, id, {
        .w = node.w > 0.0f ? node.w : uiStyle(*in.state, "range_slider", UiStyleKey::Width),
        .grow = node.grow > 0.0f ? node.grow : 1.0f,
        .shrink = 1.0f,
        .minW = node.minW > 0.0f ? node.minW : uiStyle(*in.state, "range_slider", UiStyleKey::MinW),
    });
    remember(in, node.name, id);
    return id;
}

uint16_t makeList(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiVirtual(*in.state, parent, node.h > 8.0f ? node.h : uiStyle(*in.state, "list", UiStyleKey::RowH));
    remember(in, node.name, id);
    return id;
}

uint16_t makeSpacer(Import& in, uint16_t parent, const Node&) {
    return uiSpacer(*in.state, parent, "spacer");
}

uint16_t makeCheck(Import& in, uint16_t parent, const Node& node) {
    if (node.hasBind && node.reset) {
        const uint16_t id = in.bind->checkReset(*in.state, parent, node.label, node.bind, node.on);
        remember(in, node.name, id);
        return id;
    }
    if (node.hasBind) {
        const uint16_t id = in.bind->check(*in.state, parent, node.label, node.bind, node.on);
        remember(in, node.name, id);
        return id;
    }
    const uint16_t id = uiCheck(*in.state, parent, node.label, node.kind, node.groupN, node.on);
    uiSetWidthPercent(*in.state, id, 100.0f); // a plain check fills its row
    markNode(in, id, node);
    remember(in, node.name, id);
    bindNodeCommand(in, id, node);
    return id;
}

} // namespace burnhope::propimport

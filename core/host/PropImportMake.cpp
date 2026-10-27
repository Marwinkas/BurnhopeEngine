#include "host/PropImportInternal.hpp"
#include "Str.hpp"

#include "io/File.hpp"
#include "io/JsonCursor.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Template.hpp"
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
void fillRow(UiState& s, uint16_t id) {
    if (id == kUiNone) {
        return;
    }
    uiSetWidthPercent(s, id, 100.0f);
    uiSetGrow(s, id, 1.0f, 1.0f);
}

uint16_t makeSection(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiPropSection(*in.state, parent, node.label, node.cat, node.owner);
    remember(in, node.name, id);
    return id;
}

uint16_t makeFold(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiFoldBody(*in.state, parent, node.label);
    remember(in, node.name, id);
    return id;
}

uint16_t makeGroup(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = spawnGroup(in, parent);
    remember(in, node.name, id);
    return id;
}

uint16_t makeSlider(Import& in, uint16_t parent, const Node& node) {
    uint16_t id = kUiNone;
    if (node.hasStep) {
        id = in.bind->slideStep(*in.state, parent, node.label, node.bind, node.value, node.minV, node.maxV, node.step);
    } else {
        id = in.bind->slide(*in.state, parent, node.label, node.bind, node.value, node.minV, node.maxV);
    }
    remember(in, node.name, id);
    return id;
}

uint16_t makeSwatch(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = in.bind->swatch(*in.state, parent, node.label, node.bind, node.value, node.g, node.b);
    remember(in, node.name, id);
    return id;
}

uint16_t makeVec2(Import& in, uint16_t parent, const Node& node) {
    if (node.reset && node.hasBind) {
        const UiPropVec2 home = in.bind->vec2Reset(*in.state, parent, node.label, node.bind, node.value, node.value2, node.minV, node.maxV, node.hasStep ? node.step : 0.5f);
        remember(in, node.name, home.x);
        return home.x;
    }
    const UiPropVec2 row = uiPropVec2(*in.state, parent, node.label, node.value, node.value2, node.minV, node.maxV, node.hasStep ? node.step : 0.5f);
    in.bind->watch(row.x, node.bind, node.value);
    in.bind->watch(row.y, static_cast<uint16_t>(node.bind + 1u), node.value2);
    remember(in, node.name, row.x);
    return row.x;
}

// Three consecutive scalars from `bind` as one vec3 row; "proportional" starts its lock on a ratio-keeping scale.
uint16_t makeVec3(Import& in, uint16_t parent, const Node& node) {
    const UiPropVec3 row = in.bind->vec3(*in.state, parent, node.label, node.bind, node.value, node.value2, node.value3, node.minV, node.maxV, node.hasStep ? node.step : 0.5f, node.proportional);
    remember(in, node.name, row.x);
    return row.x;
}

uint16_t makeSplit(Import& in, uint16_t parent, const Node& node) {
    return uiSplitBar(*in.state, parent, node.vertical, node.inset);
}

uint16_t makeFilter(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiPropFilterBar(*in.state, parent);
    remember(in, node.name, id);
    return id;
}

uint16_t makeHost(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiPropHost(*in.state, parent);
    remember(in, node.name, id);
    return id;
}

uint16_t makeLabel(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiLabel(*in.state, parent, node.label);
    if (node.paintName[0] != '\0' && id < in.state->count) {
        if (auto* paint = in.state->ent[id].try_get_mut<UiPaint>()) {
            *paint = paintOf(in, node);
        }
    }
    remember(in, node.name, id);
    return id;
}

uint16_t makeButton(Import& in, uint16_t parent, const Node& node) {
    uint16_t id = kUiNone;
    if (std::strcmp(node.role, "menubutton") == 0) {
        id = uiMenuButton(*in.state, parent, node.label, kUiNone);
        fillRow(*in.state, id);
    } else {
        id = uiButton(*in.state, parent, {
            .cls = node.cls[0] != '\0' ? node.cls : nullptr,
            .label = node.label,
            .iconSlot = node.hasPhoto ? node.photo : uint8_t{255},
        });
        if (node.fill && id != kUiNone) {
            // "fill": the row the button sits in gives it the full width.
            uiSetWidthPercent(*in.state, id, 100.0f);
            uiSetGrow(*in.state, id, 1.0f, 0.0f);
        }
    }
    if (node.h > 0.0f) {
        uiSetHeight(*in.state, id, node.h);
    }
    if (node.w > 0.0f) {
        uiSetWidth(*in.state, id, node.w);
    }
    markNode(in, id, node);
    if (node.row && id < in.state->count) {
        uiSetRowCentered(*in.state, id, uiStyle(*in.state, "btn_row", UiStyleKey::Gap));
    }
    remember(in, node.name, id);
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeIcon(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiButton(*in.state, parent, {.cls = node.cls[0] != '\0' ? node.cls : "tool_button", .icon = static_cast<UiIcon>(node.icon)});
    if (node.h > 0.0f) {
        uiSetSize(*in.state, id, node.h, node.h);
    }
    remember(in, node.name, id);
    remember(in, node.glyph, id, false); // the glyph is the button itself: keep the node's name
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeSlot(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiButton(*in.state, parent, {.cls = node.cls[0] != '\0' ? node.cls : "tool_button", .iconSlot = node.photo});
    if (node.h > 0.0f) {
        uiSetSize(*in.state, id, node.h, node.h);
    }
    remember(in, node.name, id);
    remember(in, node.glyph, id, false); // the icon is the button itself: bind the pointer, keep the node's name
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeUse(Import& in, uint16_t parent, const Node& node) {
    UiState& s = *in.state;
    const uint16_t id = uiSpawnTemplate(s, parent, node.use, node.cls[0] != '\0' ? node.cls : nullptr);
    if (id == kUiNone) {
        fail(in, node.useAt, "blueprint is not loaded or does not fit:", node.use);
        return kUiNone;
    }
    // The blueprint owns everything intrinsic; the instance only places it.
    UiLayoutSpec place = layoutOf(node);
    place.box = false;
    place.alignCenter = false;
    uiApplyLayout(s, id, place);
    if (node.label[0] != '\0') {
        uiText(s, id, node.label);
    }
    markNode(in, id, node);
    remember(in, node.name, id);
    bindNodeCommand(in, id, node);
    return id;
}

} // namespace burnhope::propimport

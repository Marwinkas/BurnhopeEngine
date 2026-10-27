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
#include "ui/Template.hpp"

#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstring>

namespace burnhope::propimport {
namespace {

// A "paint" name of a layout node is a style class "paint_<name>": its bg token, alpha and (when set) border.
UiPaint paintFromClass(const UiState& s, const char* cls) {
    UiStyleClass style;
    const bool known = uiStyleResolve(s, cls, style);
    UiPaint paint = paintToken(s.look, known ? style.bg : UiToken::Content, 0.0f);
    if (uiStyleHas(&style, UiStyleKey::Alpha)) {
        paint.a = style.value[static_cast<size_t>(UiStyleKey::Alpha)];
    }
    if (uiStyleHas(&style, UiStyleKey::Border)) {
        paint.borderW = style.value[static_cast<size_t>(UiStyleKey::Border)];
    }
    return paint;
}
} // namespace

UiLayoutSpec layoutOf(const Node& node) {
    return {
        .w = node.w, .h = node.h, .wp = node.wp, .hp = node.hp,
        .grow = node.grow, .shrink = node.hasShrink ? node.shrink : -1.0f,
        .gap = node.gap, .pad = node.pad, .padL = node.padL, .padR = node.padR, .padT = node.padT, .padB = node.padB,
        .minW = node.minW, .minH = node.minH, .posX = node.posX, .posY = node.posY,
        .box = true, .row = node.row, .wrap = node.wrap, .absolute = node.absolute,
        .alignCenter = node.alignCenter, .selfCenter = node.selfCenter,
    };
}

void rememberShow(Import& in, uint16_t node, const Node& spec) {
    if (!spec.hasShow || in.bind == nullptr) {
        return;
    }
    in.bind->show(node, spec.showFlag, spec.showValue, spec.showOp);
}

uint16_t spawnGroup(Import& in, uint16_t parent) {
    const uint16_t id = uiSpawnTemplate(*in.state, parent, "core/prop_group");
    if (id != kUiNone) {
        in.state->ent[id].set<UiPaint>(in.hidden);
    }
    return id;
}

void bindNodeCommand(Import& in, uint16_t id, const Node& node) {
    UiCommandPick* pick = takeCommand(in, node.command, static_cast<uint16_t>(node.value));
    if (pick != nullptr && id != kUiNone) {
        uiBind(*in.state, id, onBinderCommand, pick);
    }
}

void finishNode(Import& in, uint16_t host, const Node& node) {
    if (in.state == nullptr || host == kUiNone || host >= in.state->count) {
        return;
    }
    if (node.hide) {
        uiShow(*in.state, host, false);
    }
    if (node.center) {
        uiCenterChildren(*in.state, host);
    }
}

UiPaint paintOf(const Import& in, const Node& node) {
    const UiLook& look = in.state->look;
    if (std::strcmp(node.paintName, "veil") == 0) {
        return paintPopup(look);
    }
    if (std::strcmp(node.paintName, "glyph") == 0) {
        UiPaint paint = paintClear();
        paint.icon = node.icon;
        paintFillToken(look, paint, UiToken::Text);
        return paint;
    }
    if (std::strcmp(node.paintName, "accent") == 0) {
        UiPaint paint = paintFromClass(*in.state, "paint_accent");
        paint.token = UiToken::Custom;
        paintFillToken(look, paint, UiToken::Accent);
        return paint;
    }
    for (const char* name : {"dock", "content", "sep", "line"}) {
        if (std::strcmp(node.paintName, name) == 0) {
            char cls[24];
            std::snprintf(cls, sizeof(cls), "paint_%s", name);
            return paintFromClass(*in.state, cls);
        }
    }
    return in.hidden;
}

void markNode(Import& in, uint16_t id, const Node& node) {
    if (id == kUiNone || id >= in.state->count) {
        return;
    }
    if (node.slide) {
        in.state->ent[id].add<UiMenuSlide>();
    }
    if (node.pass) {
        uiSetPass(*in.state, id);
    }
    if (node.ghost) {
        if (auto* paint = in.state->ent[id].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
        }
    }
    if (node.hasIcon) {
        if (auto* paint = in.state->ent[id].try_get_mut<UiPaint>()) {
            paint->icon = node.icon;
        }
    }
}

uint16_t spawnPanel(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiSpawnTemplate(*in.state, parent, "core/panel");
    if (id == kUiNone) {
        return id;
    }
    in.state->ent[id].set<UiPaint>(paintOf(in, node));
    uiApplyLayout(*in.state, id, layoutOf(node));
    markNode(in, id, node);
    remember(in, node.name, id);
    return id;
}

uint16_t spawnScroll(Import& in, uint16_t parent, const Node& node) {
    UiState& s = *in.state;
    const uint16_t outer = uiSpawnTemplate(s, parent, "core/prop_scroll");
    if (outer == kUiNone) {
        return outer;
    }
    s.ent[outer].set<UiPaint>(paintOf(in, node));
    UiLayoutSpec view = layoutOf(node);
    view.scroll = true;
    uiApplyLayout(s, outer, view);
    const uint16_t inner = uiSpawnTemplate(s, outer, "core/prop_group");
    if (inner == kUiNone) {
        return inner;
    }
    s.ent[inner].set<UiPaint>(in.hidden);
    uiApplyLayout(s, inner, {.gap = node.gap, .pad = node.pad, .box = true});
    uiBindScroll(s, outer, inner);
    remember(in, node.glyph, outer);
    remember(in, node.name, inner);
    return inner;
}
} // namespace burnhope::propimport

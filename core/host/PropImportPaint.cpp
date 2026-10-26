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

void sizeFlex(UiFlex& flex, const Node& node) {
    flex.direction = node.row ? UiFlexDir::Row : UiFlexDir::Column;
    flex.grow = node.grow;
    flex.shrink = node.hasShrink ? node.shrink : flex.shrink;
    flex.gap = node.gap;
    flex.wrap = node.wrap ? UiFlexWrap::Wrap : UiFlexWrap::NoWrap;
    flex.pad = node.pad;
    flex.padL = node.padL;
    flex.padR = node.padR;
    flex.padT = node.padT;
    flex.padB = node.padB;
    flex.minW = node.minW;
    flex.minH = node.minH;
    flex.posX = node.posX;
    flex.posY = node.posY;
    if (node.wp > 0.0f) {
        flex.widthMode = UiSize::Percent;
        flex.width = node.wp;
    } else if (node.w > 0.0f) {
        flex.widthMode = UiSize::Px;
        flex.width = node.w;
    }
    if (node.hp > 0.0f) {
        flex.heightMode = UiSize::Percent;
        flex.height = node.hp;
    } else if (node.h > 0.0f) {
        flex.heightMode = UiSize::Px;
        flex.height = node.h;
    }
    if (node.absolute) {
        flex.position = UiPosition::Absolute;
    }
    if (node.alignCenter) {
        flex.align = UiAlign::Center;
    }
    if (node.selfCenter) {
        flex.self = UiAlignSelf::Center;
    }
}
} // namespace

void rememberShow(Import& in, uint16_t node, const Node& spec) {
    if (!spec.hasShow || in.bind == nullptr) {
        return;
    }
    in.bind->show(node, spec.showFlag, spec.showValue, spec.showOp);
}

uint16_t spawnGroup(Import& in, uint16_t parent) {
    UiFlex col{};
    col.direction = UiFlexDir::Column;
    col.shrink = 0.0f;
    col.widthMode = UiSize::Percent;
    col.width = 100.0f;
    return uiPanel(*in.state, parent, col, in.hidden);
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
        paint.r = look.text[0];
        paint.g = look.text[1];
        paint.b = look.text[2];
        return paint;
    }
    if (std::strcmp(node.paintName, "accent") == 0) {
        UiPaint paint = paintFromClass(*in.state, "paint_accent");
        paint.token = UiToken::Custom;
        paint.r = look.accent[0][0];
        paint.g = look.accent[0][1];
        paint.b = look.accent[0][2];
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
    UiFlex flex{};
    flex.shrink = node.hasShrink ? node.shrink : 0.0f;
    sizeFlex(flex, node);
    const uint16_t id = uiPanel(*in.state, parent, flex, paintOf(in, node));
    markNode(in, id, node);
    remember(in, node.name, id);
    return id;
}

uint16_t spawnScroll(Import& in, uint16_t parent, const Node& node) {
    UiFlex view{};
    view.shrink = 1.0f;
    sizeFlex(view, node);
    view.overflow = UiOverflow::Scroll;
    const uint16_t outer = uiPanel(*in.state, parent, view, paintOf(in, node));
    UiFlex column{};
    column.gap = node.gap;
    column.pad = node.pad;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    const uint16_t inner = uiPanel(*in.state, outer, column, in.hidden);
    uiBindScroll(*in.state, outer, inner);
    remember(in, node.glyph, outer);
    remember(in, node.name, inner);
    return inner;
}
} // namespace burnhope::propimport

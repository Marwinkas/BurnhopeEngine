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
void addMenu(Import& in, uint16_t parent, const Node& node) {
    const uint16_t menu = uiMakeMenu(*in.state);
    const char* caption = node.caption;
    if (caption[0] == '\0' && node.itemN > 0) {
        caption = node.items[0].label;
    }
    const auto selected = static_cast<uint8_t>(node.value);
    if (node.hasBind && in.bind->flags != nullptr && node.bind < in.bind->flagN) {
        in.bind->flags[node.bind] = selected;
    }
    for (uint8_t i = 0; i < node.itemN; ++i) {
        const Item& item = node.items[i];
        if (item.hasValue && item.value == selected) {
            caption = item.label;
        }
        if (item.command[0] != '\0') {
            for (uint16_t c = 0; c < in.commandCount; ++c) {
                if (in.commands[c].name == nullptr || std::strcmp(in.commands[c].name, item.command) != 0) {
                    continue;
                }
                const uint16_t arg = item.hasValue ? item.value : i;
                in.bind->commands.push_back(UiCommandPick{.fn = in.commands[c].fn, .user = in.commands[c].user, .arg = arg});
                (void)uiCommandAddTo(*in.state, menu, item.label, onBinderCommand, &in.bind->commands.back());
            }
            continue;
        }
        in.bind->menus.push_back({});
        UiMenuPick& pick = in.bind->menus.back();
        pick.bind = in.bind;
        pick.index = node.bind;
        pick.value = item.value;
        pick.onFlag = in.onFlag;
        pick.user = in.user;
        strCopyCap(pick.label, sizeof(pick.label), item.label);
        (void)uiCommandAddTo(*in.state, menu, item.label, onBinderMenu, &pick);
    }
    const uint16_t button = uiPropMenu(*in.state, parent, node.label, caption, menu);
    for (UiMenuPick& pick : in.bind->menus) {
        if (pick.button == kUiNone && pick.index == node.bind && node.hasBind) {
            pick.button = button;
        }
    }
    remember(in, node.name, button);
}

uint16_t addBar(Import& in, uint16_t parent, const Node& node) {
    const uint16_t menu = uiMakeMenu(*in.state);
    if (node.slide && menu < in.state->count) {
        in.state->ent[menu].add<UiMenuSlide>();
    }
    if (node.w > 0.0f && menu < in.state->count) {
        uiSetWidth(*in.state, menu, node.w);
    }
    uint16_t button = kUiNone;
    uint16_t glyph = kUiNone;
    if (node.iconBtn) {
        button = uiButton(*in.state, parent, {.cls = node.cls[0] != '\0' ? node.cls : "tool_button", .icon = static_cast<UiIcon>(node.icon)});
        glyph = button;
        if (node.h > 0.0f) {
            uiSetSize(*in.state, button, node.h, node.h);
        }
    } else if (node.itemN == 0) {
        button = uiMenuButton(*in.state, parent, node.label, kUiNone);
        fillRow(*in.state, button);
    } else {
        // Icon-less bar triggers (File / Edit / View) are flat: transparent, borderless, hover-lifted text.
        const bool flat = !node.hasIcon;
        button = uiButton(*in.state, parent, {.cls = flat ? "menu_trigger" : "bar_button", .label = node.label});
        if (node.w > 0.0f && button != kUiNone) {
            uiSetWidth(*in.state, button, node.w);
        }
        if (flat && button < in.state->count) {
            if (auto* paint = in.state->ent[button].try_get_mut<UiPaint>()) {
                paintHoverToken(in.state->look, *paint, UiToken::Text, uiStyle(*in.state, "menu_bar", UiStyleKey::HoverAlpha));
            }
        }
    }
    if (node.h > 0.0f) {
        uiSetHeight(*in.state, button, node.h);
    }
    if (menu != kUiNone) {
        uiSetContextMenu(*in.state, button, menu);
        uiMarkMenuBar(*in.state, button);
    }
    for (uint8_t i = 0; i < node.itemN; ++i) {
        const Item& item = node.items[i];
        const uint16_t arg = item.hasValue ? item.value : i;
        UiCommandPick* pick = takeCommand(in, item.command, arg);
        if (pick != nullptr && menu != kUiNone) {
            (void)uiCommandAddTo(*in.state, menu, item.label, onBinderCommand, pick);
        }
    }
    markNode(in, button, node);
    remember(in, node.name, button);
    remember(in, node.glyph, glyph, false);
    bindNodeCommand(in, button, node);
    return button;
}

uint16_t makeMenu(Import& in, uint16_t parent, const Node& node) {
    if (node.bar) {
        return addBar(in, parent, node);
    }
    addMenu(in, parent, node);
    return kUiNone;
}
} // namespace burnhope::propimport

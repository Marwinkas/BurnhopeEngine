#include "host/PropImport.hpp"
#include "host/PropImportInternal.hpp"
#include "Str.hpp"

#include "io/JsonCursor.hpp"
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

#include <cstring>

namespace burnhope {
namespace {

using namespace propimport;

void remember(Import& in, const char* name, uint16_t id, bool rename = true) {
    if (name == nullptr || name[0] == '\0') {
        return;
    }
    for (uint16_t i = 0; i < in.slotCount; ++i) {
        if (in.slots[i].name != nullptr && in.slots[i].id != nullptr && std::strcmp(in.slots[i].name, name) == 0) {
            *in.slots[i].id = id;
        }
    }
    if (rename && in.state != nullptr) {
        uiName(*in.state, id, name);
    }
}

void rememberShow(Import& in, uint16_t node, const Node& spec) {
    if (!spec.hasShow || in.bind == nullptr) {
        return;
    }
    in.bind->show(node, spec.showFlag, spec.showValue, spec.showOp);
}

bool parseNode(Cur& c, Import& in, uint16_t parent);

bool parseChildren(Cur& c, Import& in, uint16_t parent) {
    if (!json::eat(c, '[')) {
        return false;
    }
    json::skip(c);
    if (c.p < c.end && *c.p == ']') {
        ++c.p;
        return true;
    }
    for (;;) {
        if (!parseNode(c, in, parent)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == ']') {
            ++c.p;
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

uint16_t spawnGroup(Import& in, uint16_t parent) {
    UiFlex col{};
    col.direction = UiFlexDir::Column;
    col.shrink = 0.0f;
    col.widthMode = UiSize::Percent;
    col.width = 100.0f;
    return uiPanel(*in.state, parent, col, in.hidden);
}

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

void onChoiceCommand(void* user, uint8_t index) {
    auto* pick = static_cast<UiCommandPick*>(user);
    if (pick != nullptr && pick->fn != nullptr) {
        pick->fn(pick->user, index);
    }
}

UiCommandPick* takeCommand(Import& in, const char* name, uint16_t arg) {
    if (name == nullptr || name[0] == '\0' || in.bind == nullptr || in.commands == nullptr) {
        return nullptr;
    }
    for (uint16_t i = 0; i < in.commandCount; ++i) {
        if (in.commands[i].name == nullptr || std::strcmp(in.commands[i].name, name) != 0) {
            continue;
        }
        in.bind->commands.push_back(UiCommandPick{.fn = in.commands[i].fn, .user = in.commands[i].user, .arg = arg});
        return &in.bind->commands.back();
    }
    return nullptr;
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
    if (std::strcmp(node.paintName, "dock") == 0) {
        UiPaint paint = paintToken(look, UiToken::Content, 0.0f);
        paint.a = 0.94f;
        paint.borderW = 0.0f;
        return paint;
    }
    if (std::strcmp(node.paintName, "veil") == 0) {
        UiPaint paint = paintToken(look, UiToken::Content, look.radiusSm);
        paint.a = 0.72f;
        paint.borderW = 1.0f;
        tokenRgb(look, UiToken::Muted, paint.br, paint.bg, paint.bb);
        return paint;
    }
    if (std::strcmp(node.paintName, "content") == 0) {
        UiPaint paint = paintToken(look, UiToken::Content, 0.0f);
        paint.a = 1.0f;
        return paint;
    }
    if (std::strcmp(node.paintName, "sep") == 0) {
        UiPaint paint = paintToken(look, UiToken::Muted, 0.0f);
        paint.a = 0.55f;
        return paint;
    }
    if (std::strcmp(node.paintName, "line") == 0) {
        UiPaint paint = paintToken(look, UiToken::Muted, 0.0f);
        paint.a = 0.45f;
        return paint;
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
        UiPaint paint = paintToken(look, UiToken::Accent, 0.0f);
        paint.token = UiToken::Custom;
        paint.r = look.accent[0][0];
        paint.g = look.accent[0][1];
        paint.b = look.accent[0][2];
        paint.a = 0.0f;
        return paint;
    }
    return in.hidden;
}

void sizeFlex(UiFlex& flex, const Node& node) {
    flex.direction = node.row ? UiFlexDir::Row : UiFlexDir::Column;
    flex.grow = node.grow;
    flex.shrink = node.hasShrink ? node.shrink : flex.shrink;
    flex.gap = node.gap;
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

void markNode(Import& in, uint16_t id, const Node& node) {
    if (id == kUiNone || id >= in.state->count) {
        return;
    }
    if (node.slide) {
        in.state->ent[id].add<UiMenuSlide>();
    }
    if (node.pass) {
        in.state->ent[id].add<UiPass>();
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
        button = uiIconBtn(*in.state, parent, node.icon, node.h > 0.0f ? node.h : 28.0f, &glyph);
    } else if (node.itemN == 0) {
        button = uiMenuButton(*in.state, parent, node.label, kUiNone);
    } else {
        button = uiButton(*in.state, parent, {.label = node.label, .width = node.w, .height = node.h > 0.0f ? node.h : 26.0f});
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
    remember(in, node.glyph, glyph);
    bindNodeCommand(in, button, node);
    return button;
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
    if (in.onSwatch != nullptr) {
        in.onSwatch(in.user, *in.state, id);
    }
    remember(in, node.name, id);
    return id;
}

uint16_t makeVec2(Import& in, uint16_t parent, const Node& node) {
    const UiPropVec2 row = uiPropVec2(*in.state, parent, node.label, node.value, node.value2, node.minV, node.maxV, node.hasStep ? node.step : 0.5f);
    in.bind->watch(row.x, node.bind, node.value);
    in.bind->watch(row.y, static_cast<uint16_t>(node.bind + 1u), node.value2);
    remember(in, node.name, row.x);
    return row.x;
}

uint16_t makeMenu(Import& in, uint16_t parent, const Node& node) {
    if (node.bar) {
        return addBar(in, parent, node);
    }
    addMenu(in, parent, node);
    return kUiNone;
}

uint16_t makeSplit(Import& in, uint16_t parent, const Node& node) {
    return uiSplitBar(*in.state, parent, node.vertical);
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
    } else {
        id = uiButton(*in.state, parent, {
            .label = node.label,
            .width = node.w,
            .height = node.h,
            .fillWidth = node.fill,
            .iconSlot = node.hasPhoto ? node.photo : uint8_t{255},
        });
    }
    if (node.h > 0.0f) {
        uiSetHeight(*in.state, id, node.h);
    }
    if (node.w > 0.0f) {
        uiSetWidth(*in.state, id, node.w);
    }
    markNode(in, id, node);
    if (node.row && id < in.state->count) {
        if (auto* flex = in.state->ent[id].try_get_mut<UiFlex>()) {
            flex->direction = UiFlexDir::Row;
            flex->align = UiAlign::Center;
            flex->justify = UiJustify::Center;
            flex->gap = 2.0f;
        }
    }
    remember(in, node.name, id);
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeIcon(Import& in, uint16_t parent, const Node& node) {
    uint16_t glyph = kUiNone;
    const uint16_t id = uiIconBtn(*in.state, parent, node.icon, node.h > 0.0f ? node.h : 28.0f, &glyph);
    remember(in, node.name, id);
    remember(in, node.glyph, glyph);
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeSlot(Import& in, uint16_t parent, const Node& node) {
    uint16_t glyph = kUiNone;
    const uint16_t id = uiSlotBtn(*in.state, parent, node.photo, node.h > 0.0f ? node.h : 28.0f, &glyph);
    remember(in, node.name, id);
    remember(in, node.glyph, glyph, false); // the icon is the button itself: bind the pointer, keep the node's name
    bindNodeCommand(in, id, node);
    return id;
}

uint16_t makeChoice(Import& in, uint16_t parent, const Node& node) {
    const char* labels[8]{};
    for (uint8_t i = 0; i < node.itemN && i < 8; ++i) {
        labels[i] = node.items[i].label;
    }
    const float width = uiChoiceWidth(*in.state, labels, node.itemN);
    UiCommandPick* pick = takeCommand(in, node.command, 0);
    const uint16_t id = uiChoice(*in.state, parent, {
        .labels = labels,
        .count = node.itemN,
        .selected = static_cast<uint8_t>(node.value),
        .width = width,
        .onPick = pick != nullptr ? onChoiceCommand : nullptr,
        .user = pick,
    });
    uiChoiceFit(*in.state, id, width);
    remember(in, node.name, id);
    return id;
}

uint16_t makeNumber(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiNumber(*in.state, parent, node.value, node.minV, node.maxV, node.hasStep ? node.step : 0.01f);
    uiSetHeight(*in.state, id, node.h > 0.0f ? node.h : 28.0f);
    if (node.w > 0.0f) {
        uiSetWidth(*in.state, id, node.w);
    }
    uiNumberFill(*in.state, id, false);
    if (id < in.state->count) {
        if (auto* flex = in.state->ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = node.w > 0.0f ? node.w : 72.0f;
            flex->grow = 0.0f;
            flex->shrink = 0.0f;
        }
    }
    remember(in, node.name, id);
    return id;
}

uint16_t makeRange(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiSlider(*in.state, parent, node.value, node.minV, node.maxV, nullptr, nullptr, node.w > 0.0f ? node.w : 32.0f);
    if (id < in.state->count) {
        if (auto* flex = in.state->ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = node.w > 0.0f ? node.w : 32.0f;
            flex->grow = node.grow > 0.0f ? node.grow : 1.0f;
            flex->shrink = 1.0f;
            flex->minW = node.minW > 0.0f ? node.minW : 24.0f;
        }
    }
    remember(in, node.name, id);
    return id;
}

uint16_t makeList(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiVirtual(*in.state, parent, node.h > 8.0f ? node.h : 28.0f);
    remember(in, node.name, id);
    return id;
}

uint16_t makeSpacer(Import& in, uint16_t parent, const Node&) {
    UiFlex gap{};
    gap.grow = 1.0f;
    gap.shrink = 1.0f;
    gap.widthMode = UiSize::Px;
    gap.width = 8.0f;
    gap.heightMode = UiSize::Px;
    gap.height = 1.0f;
    return uiSpacer(*in.state, parent, gap);
}

uint16_t makeCheck(Import& in, uint16_t parent, const Node& node) {
    if (node.hasBind) {
        const uint16_t id = in.bind->check(*in.state, parent, node.label, node.bind, node.on);
        remember(in, node.name, id);
        return id;
    }
    const uint16_t id = uiCheck(*in.state, parent, node.label, node.kind, node.groupN, node.on);
    markNode(in, id, node);
    remember(in, node.name, id);
    bindNodeCommand(in, id, node);
    return id;
}

struct RoleSpawn {
    const char* role;
    uint16_t (*make)(Import& in, uint16_t parent, const Node& node);
};

constexpr RoleSpawn kRoleSpawns[] = {
    {"section", makeSection},
    {"fold", makeFold},
    {"group", makeGroup},
    {"slider", makeSlider},
    {"swatch", makeSwatch},
    {"vec2", makeVec2},
    {"menu", makeMenu},
    {"panel", spawnPanel},
    {"sep", spawnPanel},
    {"scroll", spawnScroll},
    {"split", makeSplit},
    {"filter", makeFilter},
    {"host", makeHost},
    {"label", makeLabel},
    {"button", makeButton},
    {"menubutton", makeButton},
    {"icon", makeIcon},
    {"slot", makeSlot},
    {"choice", makeChoice},
    {"number", makeNumber},
    {"range", makeRange},
    {"list", makeList},
    {"spacer", makeSpacer},
    {"check", makeCheck},
};

uint16_t spawn(Import& in, uint16_t parent, const Node& node) {
    for (const RoleSpawn& entry : kRoleSpawns) {
        if (std::strcmp(node.role, entry.role) == 0) {
            return entry.make(in, parent, node);
        }
    }
    return parent;
}

bool parseNode(Cur& c, Import& in, uint16_t parent) {
    if (!json::eat(c, '{')) {
        return false;
    }
    Node node{};
    bool spawned = false;
    uint16_t host = parent;
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        ++c.p;
        return true;
    }
    for (;;) {
        char key[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, key, sizeof(key), n) || !json::eat(c, ':')) {
            return false;
        }
        if (keyIs(key, "role")) {
            if (!json::parseStringCap(c, node.role, sizeof(node.role), n)) {
                return false;
            }
        } else if (keyIs(key, "label")) {
            if (!json::parseStringCap(c, node.label, sizeof(node.label), n)) {
                return false;
            }
        } else if (keyIs(key, "name")) {
            if (!json::parseStringCap(c, node.name, sizeof(node.name), n)) {
                return false;
            }
        } else if (keyIs(key, "caption")) {
            if (!json::parseStringCap(c, node.caption, sizeof(node.caption), n)) {
                return false;
            }
        } else if (keyIs(key, "bind")) {
            double number = 0.0;
            if (!json::parseNumber(c, number)) {
                return false;
            }
            node.bind = static_cast<uint16_t>(number);
            node.hasBind = true;
        } else if (keyIs(key, "cat") || keyIs(key, "owner")) {
            double number = 0.0;
            if (!json::parseNumber(c, number)) {
                return false;
            }
            if (keyIs(key, "cat")) {
                node.cat = static_cast<uint8_t>(number);
            } else {
                node.owner = static_cast<uint8_t>(number);
            }
        } else if (keyIs(key, "value") || keyIs(key, "value2") || keyIs(key, "min") || keyIs(key, "max") || keyIs(key, "step") || keyIs(key, "g") || keyIs(key, "b")) {
            double number = 0.0;
            if (!json::parseNumber(c, number)) {
                return false;
            }
            const float v = static_cast<float>(number);
            if (keyIs(key, "value")) {
                node.value = v;
            } else if (keyIs(key, "value2")) {
                node.value2 = v;
            } else if (keyIs(key, "min")) {
                node.minV = v;
            } else if (keyIs(key, "max")) {
                node.maxV = v;
            } else if (keyIs(key, "step")) {
                node.step = v;
                node.hasStep = true;
            } else if (keyIs(key, "g")) {
                node.g = v;
            } else {
                node.b = v;
            }
        } else if (keyIs(key, "command")) {
            if (!json::parseStringCap(c, node.command, sizeof(node.command), n)) {
                return false;
            }
        } else if (keyIs(key, "glyph")) {
            if (!json::parseStringCap(c, node.glyph, sizeof(node.glyph), n)) {
                return false;
            }
        } else if (keyIs(key, "paint") || keyIs(key, "dir") || keyIs(key, "align") || keyIs(key, "self")) {
            char text[16]{};
            if (!json::parseStringCap(c, text, sizeof(text), n)) {
                return false;
            }
            if (keyIs(key, "paint")) {
                strCopyCap(node.paintName, sizeof(node.paintName), text);
            } else if (keyIs(key, "dir")) {
                node.row = std::strcmp(text, "row") == 0;
            } else if (keyIs(key, "align")) {
                node.alignCenter = std::strcmp(text, "center") == 0;
            } else {
                node.selfCenter = std::strcmp(text, "center") == 0;
            }
        } else if (keyIs(key, "icon") || keyIs(key, "photo") || keyIs(key, "kind") || keyIs(key, "group")
            || keyIs(key, "w") || keyIs(key, "h") || keyIs(key, "wp") || keyIs(key, "hp")
            || keyIs(key, "grow") || keyIs(key, "shrink") || keyIs(key, "gap") || keyIs(key, "pad")
            || keyIs(key, "padL") || keyIs(key, "padR") || keyIs(key, "padT") || keyIs(key, "padB")
            || keyIs(key, "minW") || keyIs(key, "minH") || keyIs(key, "posX") || keyIs(key, "posY")) {
            double number = 0.0;
            if (!json::parseNumber(c, number)) {
                return false;
            }
            const float v = static_cast<float>(number);
            if (keyIs(key, "icon")) {
                node.icon = static_cast<uint8_t>(number);
                node.hasIcon = true;
            } else if (keyIs(key, "photo")) {
                node.photo = static_cast<uint8_t>(number);
                node.hasPhoto = true;
            } else if (keyIs(key, "kind")) {
                node.kind = static_cast<uint8_t>(number);
            } else if (keyIs(key, "group")) {
                node.groupN = static_cast<uint8_t>(number);
            } else if (keyIs(key, "w")) {
                node.w = v;
            } else if (keyIs(key, "h")) {
                node.h = v;
            } else if (keyIs(key, "wp")) {
                node.wp = v;
            } else if (keyIs(key, "hp")) {
                node.hp = v;
            } else if (keyIs(key, "grow")) {
                node.grow = v;
            } else if (keyIs(key, "shrink")) {
                node.shrink = v;
                node.hasShrink = true;
            } else if (keyIs(key, "gap")) {
                node.gap = v;
            } else if (keyIs(key, "pad")) {
                node.pad = v;
            } else if (keyIs(key, "padL")) {
                node.padL = v;
            } else if (keyIs(key, "padR")) {
                node.padR = v;
            } else if (keyIs(key, "padT")) {
                node.padT = v;
            } else if (keyIs(key, "padB")) {
                node.padB = v;
            } else if (keyIs(key, "minW")) {
                node.minW = v;
            } else if (keyIs(key, "minH")) {
                node.minH = v;
            } else if (keyIs(key, "posX")) {
                node.posX = v;
            } else {
                node.posY = v;
            }
        } else if (keyIs(key, "bar") || keyIs(key, "slide") || keyIs(key, "hide") || keyIs(key, "center")
            || keyIs(key, "ghost") || keyIs(key, "pass") || keyIs(key, "fill") || keyIs(key, "absolute")
            || keyIs(key, "vertical") || keyIs(key, "iconbtn")) {
            json::skip(c);
            bool flag = false;
            if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
                flag = true;
                c.p += 4;
            } else if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
                c.p += 5;
            } else {
                return false;
            }
            if (keyIs(key, "bar")) {
                node.bar = flag;
            } else if (keyIs(key, "slide")) {
                node.slide = flag;
            } else if (keyIs(key, "hide")) {
                node.hide = flag;
            } else if (keyIs(key, "center")) {
                node.center = flag;
            } else if (keyIs(key, "ghost")) {
                node.ghost = flag;
            } else if (keyIs(key, "pass")) {
                node.pass = flag;
            } else if (keyIs(key, "fill")) {
                node.fill = flag;
            } else if (keyIs(key, "absolute")) {
                node.absolute = flag;
            } else if (keyIs(key, "vertical")) {
                node.vertical = flag;
            } else {
                node.iconBtn = flag;
            }
        } else if (keyIs(key, "on")) {
            json::skip(c);
            if (c.p + 4 <= c.end && std::memcmp(c.p, "true", 4) == 0) {
                node.on = true;
                c.p += 4;
            } else if (c.p + 5 <= c.end && std::memcmp(c.p, "false", 5) == 0) {
                node.on = false;
                c.p += 5;
            } else {
                return false;
            }
        } else if (keyIs(key, "items")) {
            if (!parseItems(c, node)) {
                return false;
            }
        } else if (keyIs(key, "showWhen")) {
            if (!parseShow(c, node)) {
                return false;
            }
            if (spawned) {
                rememberShow(in, host, node);
            }
        } else if (keyIs(key, "children")) {
            if (!spawned) {
                host = spawn(in, parent, node);
                spawned = true;
                rememberShow(in, host, node);
            }
            if (!parseChildren(c, in, host)) {
                return false;
            }
            finishNode(in, host, node);
        } else if (!json::skipValue(c)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            if (!spawned) {
                host = spawn(in, parent, node);
                rememberShow(in, host, node);
            }
            finishNode(in, host, node);
            return true;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

bool importArray(Cur& c, Import& in, uint16_t parent) {
    return parseChildren(c, in, parent);
}

} // namespace

bool propImport(
    UiState& state,
    uint16_t parent,
    std::string_view text,
    const char* key,
    UiPropBinder& bind,
    const PropSlot* slots,
    uint16_t slotCount,
    const PropCommand* commands,
    uint16_t commandCount,
    const UiPaint& hidden,
    PropFlagFn onFlag,
    PropSwatchFn onSwatch,
    void* user) {
    if (key == nullptr || text.data() == nullptr) {
        return false;
    }
    Cur c{text.data(), text.data() + text.size()};
    if (!json::eat(c, '{')) {
        return false;
    }
    Import in{
        .state = &state,
        .bind = &bind,
        .slots = slots,
        .slotCount = slotCount,
        .commands = commands,
        .commandCount = commandCount,
        .hidden = hidden,
        .onFlag = onFlag,
        .onSwatch = onSwatch,
        .user = user,
    };
    bool found = false;
    json::skip(c);
    if (c.p < c.end && *c.p == '}') {
        return false;
    }
    for (;;) {
        char name[16]{};
        uint32_t n = 0;
        if (!json::parseStringCap(c, name, sizeof(name), n) || !json::eat(c, ':')) {
            return false;
        }
        if (std::strcmp(name, key) == 0) {
            if (!importArray(c, in, parent)) {
                return false;
            }
            found = true;
        } else if (!json::skipValue(c)) {
            return false;
        }
        json::skip(c);
        if (c.p < c.end && *c.p == '}') {
            ++c.p;
            return found;
        }
        if (!json::eat(c, ',')) {
            return false;
        }
    }
}

} // namespace burnhope

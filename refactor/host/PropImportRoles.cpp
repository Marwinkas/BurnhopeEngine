#include "host/PropImportInternal.hpp"

#include "ui/widget/Image.hpp"
#include "ui/widget/Segment.hpp"
#include "ui/State.hpp"

#include <cstring>

namespace burnhope::propimport {

void remember(Import& in, const char* name, uint16_t id, bool rename) {
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

namespace {

// A row of exclusive buttons; "command" receives the picked index, "value" is the starting one.
uint16_t makeSegment(Import& in, uint16_t parent, const Node& node) {
    const char* labels[8]{};
    for (uint8_t i = 0; i < node.itemN && i < 8; ++i) {
        labels[i] = node.items[i].label;
    }
    UiCommandPick* pick = takeCommand(in, node.command, 0);
    const uint16_t id = uiSegment(*in.state, parent, {
        .labels = labels,
        .count = node.itemN,
        .selected = static_cast<uint8_t>(node.value),
        .onPick = pick != nullptr ? onChoiceCommand : nullptr,
        .user = pick,
    });
    remember(in, node.name, id);
    return id;
}

// A photo-slot image box of w x h px; the host wires its drag handlers after the import through the name.
uint16_t makePreview(Import& in, uint16_t parent, const Node& node) {
    const uint16_t id = uiIcon(*in.state, parent, node.photo, node.w, node.h);
    remember(in, node.name, id);
    return id;
}

// A property row with a choice bound to flag `bind` and a reset button; "command" receives the picked index.
uint16_t makePick(Import& in, uint16_t parent, const Node& node) {
    const char* labels[8]{};
    for (uint8_t i = 0; i < node.itemN && i < 8; ++i) {
        labels[i] = node.items[i].label;
    }
    UiCommandPick* pick = takeCommand(in, node.command, 0);
    const uint16_t id = in.bind->choiceReset(*in.state, parent, node.label, labels, node.itemN, pick != nullptr ? onChoiceCommand : nullptr, pick, node.bind);
    remember(in, node.name, id);
    return id;
}

uint16_t makeHosted(Import& in, uint16_t parent, const Node& node, const PropRole& role) {
    const PropSpec spec{
        .label = node.label,
        .name = node.name,
        .bind = node.bind,
        .value = node.value,
        .minV = node.minV,
        .maxV = node.maxV,
        .kind = node.kind,
        .group = node.groupN,
        .slot = node.slotN,
        .channel = node.channel,
        .cell = node.cell,
    };
    const uint16_t id = role.make(role.user, *in.state, *in.bind, parent, spec);
    remember(in, node.name, id);
    return id;
}

struct RoleSpawn {
    const char* role;
    uint16_t (*make)(Import& in, uint16_t parent, const Node& node);
};

constexpr RoleSpawn kRoleSpawns[] = {
    {"section", makeSection}, {"fold", makeFold}, {"group", makeGroup}, {"slider", makeSlider},
    {"swatch", makeSwatch}, {"vec2", makeVec2}, {"vec3", makeVec3}, {"menu", makeMenu},
    {"panel", spawnPanel}, {"sep", spawnPanel}, {"scroll", spawnScroll}, {"split", makeSplit},
    {"filter", makeFilter}, {"host", makeHost}, {"label", makeLabel}, {"button", makeButton},
    {"menubutton", makeButton}, {"icon", makeIcon}, {"slot", makeSlot}, {"choice", makeChoice},
    {"number", makeNumber}, {"range", makeRange}, {"list", makeList}, {"spacer", makeSpacer},
    {"check", makeCheck}, {"segment", makeSegment}, {"preview", makePreview}, {"pick", makePick},
};

} // namespace

bool spawnRole(Import& in, uint16_t parent, const Node& node, uint16_t& out) {
    if (node.use[0] != '\0') {
        out = makeUse(in, parent, node);
        return out != kUiNone;
    }
    for (const RoleSpawn& entry : kRoleSpawns) {
        if (std::strcmp(node.role, entry.role) == 0) {
            out = entry.make(in, parent, node);
            return true;
        }
    }
    for (uint16_t i = 0; i < in.roleCount; ++i) {
        if (in.roles[i].role != nullptr && in.roles[i].make != nullptr && std::strcmp(node.role, in.roles[i].role) == 0) {
            out = makeHosted(in, parent, node, in.roles[i]);
            return true;
        }
    }
    return false;
}

} // namespace burnhope::propimport

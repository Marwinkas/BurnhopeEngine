#pragma once

#include "host/PropImport.hpp"
#include "io/JsonCursor.hpp"
#include "ui/Style.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/widget/Number.hpp"

#include <cstdint>

namespace burnhope::propimport {

using json::Cur;

struct Item {
    char label[48]{};
    char command[48]{};
    uint8_t value = 0;
    bool hasValue = false;
};

struct Node {
    char role[16]{};
    char label[64]{};
    char name[32]{};
    char caption[48]{};
    uint16_t bind = 0;
    uint8_t cat = 0;
    uint8_t owner = 0;
    float value = 0.0f;
    float value2 = 0.0f;
    float minV = 0.0f;
    float maxV = 1.0f;
    float step = 0.01f;
    float g = 0.0f;
    float b = 0.0f;
    bool hasBind = false;
    bool hasStep = false;
    bool on = false;
    bool hasShow = false;
    uint16_t showFlag = 0;
    uint8_t showValue = 0;
    uint8_t showOp = 0;
    char command[48]{};
    char glyph[32]{};
    char paintName[16]{};
    uint8_t icon = 255;
    uint8_t photo = 255;
    uint8_t kind = 0;
    uint8_t groupN = 0;
    uint8_t slotN = 0;
    uint8_t channel = 0;
    uint8_t cell = 0;
    bool reset = false;
    float w = 0.0f;
    float h = 0.0f;
    float wp = 0.0f;
    float hp = 0.0f;
    float grow = 0.0f;
    float shrink = 1.0f;
    float gap = 0.0f;
    float pad = 0.0f;
    float padL = 0.0f;
    float padR = 0.0f;
    float padT = 0.0f;
    float padB = 0.0f;
    float minW = 0.0f;
    float minH = 0.0f;
    float posX = 0.0f;
    float posY = 0.0f;
    bool hasShrink = false;
    bool row = false;
    bool bar = false;
    bool slide = false;
    bool hide = false;
    bool center = false;
    bool ghost = false;
    bool pass = false;
    bool fill = false;
    bool absolute = false;
    bool vertical = false;
    bool alignCenter = false;
    bool selfCenter = false;
    bool hasIcon = false;
    bool hasPhoto = false;
    bool iconBtn = false;
    uint8_t itemN = 0;
    Item items[8]{};
};

struct Import {
    UiState* state = nullptr;
    UiPropBinder* bind = nullptr;
    const PropSlot* slots = nullptr;
    uint16_t slotCount = 0;
    const PropCommand* commands = nullptr;
    uint16_t commandCount = 0;
    UiPaint hidden{};
    PropFlagFn onFlag = nullptr;
    void* user = nullptr;
    const PropRole* roles = nullptr;
    uint16_t roleCount = 0;
};

// Records `id` in the slot list under `name` and, when `rename`, names the node.
void remember(Import& in, const char* name, uint16_t id, bool rename = true);
// The binder-owned command callback registered under `name`, or nullptr.
UiCommandPick* takeCommand(Import& in, const char* name, uint16_t arg);
void onChoiceCommand(void* user, uint8_t index);
// Builds the roles that live outside the main table (segment, preview, pick and host-registered PropRoles).
// Returns false when `node.role` is none of them.
bool spawnExtra(Import& in, uint16_t parent, const Node& node, uint16_t& out);
// Keys of the node object that parseNode does not own: slot, channel, cell, reset. Unknown keys are skipped.
bool parseExtra(Cur& c, const char* key, Node& node);

bool keyIs(const char* key, const char* name);
bool parseItem(Cur& c, Item& item);
bool parseShow(Cur& c, Node& node);
// Copies the size and spacing of style class `name` onto `node`. A node lists "class" before its own keys,
// so the keys that follow override what the class set.
void applyClass(const UiState& s, Node& node, const char* name);
bool parseItems(Cur& c, Node& node);

} // namespace burnhope::propimport

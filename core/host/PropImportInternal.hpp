#pragma once

#include "host/PropImport.hpp"
#include "io/JsonCursor.hpp"
#include "ui/Panel.hpp"
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
    char cls[24]{};             // "class": applied after every key is read
    const char* clsAt = nullptr; // where "class" appeared, for the error message
    char use[24]{};             // "use": a blueprint of assets/ui/core spawned in place of a role
    const char* useAt = nullptr;
    uint16_t bind = 0;
    uint8_t cat = 0;
    uint8_t owner = 0;
    float value = 0.0f;
    float value2 = 0.0f;
    float value3 = 0.0f;
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
    float inset = 0.0f;
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
    bool wrap = false;
    bool proportional = false;
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
    const char* asset = nullptr; // document name for error messages
    const char* text = nullptr;  // start of the document, for line and column
    bool failed = false;         // an error was already logged
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

// Logs "<asset>:<line>:<col>: <message>" for the document position `at` and marks the import failed.
// Always returns false so a parser can `return fail(...)`.
bool fail(Import& in, const char* at, const char* message, const char* detail = "");

// Records `id` in the slot list under `name` and, when `rename`, names the node.
void remember(Import& in, const char* name, uint16_t id, bool rename = true);
// The binder-owned command callback registered under `name`, or nullptr.
UiCommandPick* takeCommand(Import& in, const char* name, uint16_t arg);
void onChoiceCommand(void* user, uint8_t index);

// Reads one node object up to (not into) its "children" array: every key fills `node`, in any order.
// `children` is left at the array, or null when the node has none. Unknown keys, bad value types and bad
// enum words fail the import.
bool parseNodeKeys(Cur& c, Import& in, Node& node, const char*& children);
bool parseItem(Import& in, Cur& c, Item& item);
bool parseShow(Import& in, Cur& c, Node& node);
bool parseItems(Import& in, Cur& c, Node& node);
// Copies the size and spacing of style class `name` onto `node`; fields the node set itself keep their value.
// Fails when neither styles.json nor the built-in classes know `name`.
bool applyClass(Import& in, const char* at, Node& node, const char* name);

// A menu button spans its row and shares the row's free width, unless the layout sizes it.
void fillRow(UiState& s, uint16_t id);

// Builds `node` under `parent` with the maker of node.role: a built-in role, segment / preview / pick, or a
// host-registered PropRole. Returns false for an unknown role.
bool spawnRole(Import& in, uint16_t parent, const Node& node, uint16_t& out);

// Makers shared by the role table (PropImportRoles.cpp) and the files that define them.
// The placement a layout node asks for: size, grow, gap, padding and position.
UiLayoutSpec layoutOf(const Node& node);
uint16_t spawnPanel(Import& in, uint16_t parent, const Node& node);
uint16_t spawnScroll(Import& in, uint16_t parent, const Node& node);
uint16_t makeMenu(Import& in, uint16_t parent, const Node& node);
void bindNodeCommand(Import& in, uint16_t id, const Node& node);
void markNode(Import& in, uint16_t id, const Node& node);
void rememberShow(Import& in, uint16_t node, const Node& spec);
void finishNode(Import& in, uint16_t host, const Node& node);
UiPaint paintOf(const Import& in, const Node& node);
uint16_t spawnGroup(Import& in, uint16_t parent);

uint16_t makeSection(Import& in, uint16_t parent, const Node& node);
uint16_t makeFold(Import& in, uint16_t parent, const Node& node);
uint16_t makeGroup(Import& in, uint16_t parent, const Node& node);
uint16_t makeSlider(Import& in, uint16_t parent, const Node& node);
uint16_t makeSwatch(Import& in, uint16_t parent, const Node& node);
uint16_t makeVec2(Import& in, uint16_t parent, const Node& node);
uint16_t makeVec3(Import& in, uint16_t parent, const Node& node);
uint16_t makeSplit(Import& in, uint16_t parent, const Node& node);
uint16_t makeFilter(Import& in, uint16_t parent, const Node& node);
uint16_t makeHost(Import& in, uint16_t parent, const Node& node);
uint16_t makeLabel(Import& in, uint16_t parent, const Node& node);
uint16_t makeButton(Import& in, uint16_t parent, const Node& node);
uint16_t makeIcon(Import& in, uint16_t parent, const Node& node);
uint16_t makeSlot(Import& in, uint16_t parent, const Node& node);
uint16_t makeChoice(Import& in, uint16_t parent, const Node& node);
uint16_t makeNumber(Import& in, uint16_t parent, const Node& node);
uint16_t makeRange(Import& in, uint16_t parent, const Node& node);
uint16_t makeList(Import& in, uint16_t parent, const Node& node);
uint16_t makeSpacer(Import& in, uint16_t parent, const Node& node);
uint16_t makeCheck(Import& in, uint16_t parent, const Node& node);
// Spawns the blueprint node.use names; the node supplies placement, label and icon. kUiNone (import failed) on error.
uint16_t makeUse(Import& in, uint16_t parent, const Node& node);

} // namespace burnhope::propimport

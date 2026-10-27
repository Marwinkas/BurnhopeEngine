#pragma once

#include "ui/Flex.hpp"
#include "ui/Style.hpp"
#include "ui/Types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace burnhope {

constexpr uint8_t kUiTemplateCap = 64;
constexpr uint16_t kUiTplPartCap = 512;
constexpr uint8_t kUiTplDepth = 8;
constexpr uint8_t kUiTplSlotCap = 8;
constexpr uint8_t kUiTraitCap = 16;
constexpr uint8_t kUiTemplateNone = 0xff;

using UiTemplateId = uint8_t;
using UiTraitFn = void (*)(UiState& s, uint16_t id);

// One node of a blueprint: its own flex block and the name of its style class, which uiSpawnTemplate resolves
// against the sheet loaded at that moment (so a sheet reload shows on the next spawn). Parts are stored in
// pre-order, so part `i` of a template spawned at root id R becomes node R + i.
struct alignas(16) UiTplPart {
    UiFlex flex{};
    char cls[24]{};
    uint32_t traits = 0;   // bit i = trait i of the UiTraitTable
    uint16_t parent = kUiNone;
    uint8_t hasText = 0;
    uint8_t textAlign = 0;
    uint8_t textValign = 0;
    uint8_t textEllipsis = 0;
    uint8_t textWrap = 0;
    float textLeading = 0.0f;
    char role[16]{};
    char name[16]{};
};

struct UiTplSlot {
    char name[12]{};
    uint16_t part = 0;
};

struct UiTemplate {
    char name[24]{};
    uint32_t nameHash = 0;
    uint16_t first = 0;
    uint16_t count = 0;
    UiTplSlot slot[kUiTplSlotCap]{};
    uint8_t slotCount = 0;
};

// Flecs singleton beside UiStyleSheet. Replaced as a whole by uiTemplatesLoad.
struct UiTemplateSheet {
    UiTemplate tpl[kUiTemplateCap]{};
    UiTplPart part[kUiTplPartCap]{};
    uint16_t partCount = 0;
    uint8_t count = 0;
};

// Source of one blueprint: the name nodes look it up by ("core/button") and its JSON text.
struct UiTemplateSource {
    const char* name = nullptr;
    std::string_view text;
};

// Registers a trait a blueprint can list in "traits". The built-ins are hoverable, focusable, text_input, nohit, pass,
// popup and the markers pressable / draggable (which add nothing). Widgets register their own (resizable, ...) before loading.
// A null `fn` makes the trait a marker. Registering a name twice keeps the first. False when the table is full.
bool uiTemplateTrait(UiState& s, const char* name, UiTraitFn fn);

// Replaces the registry with the blueprints in `sources`; load them after the style sheet, which they are checked
// against. Returns false (registry untouched) when a document does not parse, lists a key a blueprint may not carry
// (grow, self, w, h, margins, position, ...), names an unknown class, trait or slot part, or exceeds the caps. Errors
// are logged as "<name>:<line>:<col>: ...".
//
// Blueprint layout:
//   {"role": "button", "class": "btn_default",
//    "flex": {"dir": "row", "align": "center", "justify": "center", "gap": 4, "pad": 2, "shrink": 0},
//    "text": {"align": "center", "valign": "center", "ellipsis": "end", "wrap": false, "leading": 1},
//    "traits": ["hoverable", "focusable", "clip"],
//    "parts": [{"name": "icon", "role": "icon", "class": "...", "flex": {...}, "parts": [...]},
//              {"use": "core/button", "name": "day", "class": "cal_day", "count": 7}],
//    "slots": {"content": "icon"}}
// A part may "use" an earlier one-part blueprint (it must be the part's first key; the other keys then refine it) and
// "count" repeats it (with its children) side by side. "shrink" may be 0 for a control that must keep its content width. Placement (grow, self, w, h, margins, position) is
// the instance's business, never the blueprint's.
bool uiTemplatesLoad(UiState& s, std::span<const UiTemplateSource> sources);
// Loads the blueprints listed in a manifest asset, {"templates": ["core/button", ...]}; "core/button" is read from
// assets/ui/core/button.json.
bool uiTemplatesLoadAsset(UiState& s, const char* manifest);

[[nodiscard]] UiTemplateId uiTemplateFind(const UiState& s, const char* name);

// Spawns the blueprint under `parent` and returns the root node, or kUiNone when the id is unknown, `cls` names no
// class, or the nodes would pass kUiCap (nothing is spawned then). A non-null `cls` is laid over the root part's own
// class: keys it sets win, the rest keep the blueprint's values. Class keys drive the node: sizes, padding, gap,
// bg / activeBg tokens, alpha, border, radius, shadow and plate.
[[nodiscard]] uint16_t uiSpawnTemplate(UiState& s, uint16_t parent, UiTemplateId id, const char* cls = nullptr);
[[nodiscard]] uint16_t uiSpawnTemplate(UiState& s, uint16_t parent, const char* name, const char* cls = nullptr);
// Re-skins the existing node `id` as a one-part blueprint: its flex and paint are replaced by the blueprint's (with
// `cls` laid over the root class), its text keeps the label and takes the blueprint's text style, and the traits are
// added. False when the blueprint or class is unknown.
bool uiApplyTemplate(UiState& s, uint16_t id, UiTemplateId tpl, const char* cls = nullptr);
// The node of slot `slot` in the instance rooted at `root`, or kUiNone.
[[nodiscard]] uint16_t uiTemplateSlot(const UiState& s, UiTemplateId id, uint16_t root, const char* slot);

} // namespace burnhope

#include "ui/widget/Search.hpp"

#include "Str.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"
#include "ui/Template.hpp"

namespace burnhope {
namespace {

constexpr float kBinSearchWidth = 180.0f;

uint16_t loadSearch(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char*, const char*, const char* hint, void*) {
    if (hint != nullptr && hint[0] == '\0') {
        hint = nullptr;
    }
    const uint16_t id = uiSearchField(s, parent, hint, nullptr, nullptr);
    // A binary layout places its search box: its own width, else the usual 180.
    uiSetWidth(s, id, node.width > 1.0f ? node.width : kBinSearchWidth);
    return id;
}

} // namespace

uint16_t uiSearchField(UiState& s, uint16_t parent, const char* hint, UiTextChangeFn onChange, void* user) {
    const uint16_t id = uiSpawnTemplate(s, parent, "core/search");
    if (id == kUiNone) {
        return id;
    }
    UiField field{};
    copyText(field.hint, field.hintLen, hint != nullptr ? hint : "");
    s.ent[id].set<UiField>(field);
    fieldSyncText(s, id);
    if (onChange != nullptr) {
        s.ent[id].set<UiTextHook>({onChange, user});
    }
    return id;
}

void uiBindSearch(UiState& s, const char* name, UiTextChangeFn onChange, void* user) {
    const uint16_t id = uiFindName(s, name);
    if (id >= s.count || onChange == nullptr) {
        return;
    }
    s.ent[id].set<UiTextHook>({onChange, user});
}

void uiSearchBin(UiState& s) {
    uiBinRole(s, kBinSlotSearch, "search", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotSearch, loadSearch, nullptr);
}

} // namespace burnhope

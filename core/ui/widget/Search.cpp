#include "ui/widget/Search.hpp"

#include "Str.hpp"
#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/State.hpp"

namespace burnhope {
namespace {

uint16_t loadSearch(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char*, const char*, const char* hint, void*) {
    const float width = node.width > 1.0f ? node.width : 180.0f;
    if (hint != nullptr && hint[0] == '\0') {
        hint = nullptr;
    }
    return uiSearchField(s, parent, hint, nullptr, nullptr, width);
}

} // namespace

uint16_t uiSearchField(UiState& s, uint16_t parent, const char* hint, UiTextChangeFn onChange, void* user, float width) {
    UiFlex flex{};
    flex.shrink = 0.0f;
    flex.heightMode = UiSize::Px;
    flex.height = s.metrics.searchH;
    if (width < 0.0f) {
        flex.widthMode = UiSize::Percent;
        flex.width = 100.0f;
        flex.grow = 1.0f;
    } else {
        flex.widthMode = UiSize::Px;
        flex.width = width > 1.0f ? width : 160.0f;
    }
    const uint16_t id = uiPanel(s, parent, flex, paintField(s.look));
    if (id == kUiNone) {
        return id;
    }
    uiHover(s, id);
    uiSetFocusable(s, id);
    s.ent[id].add<UiTextInput>();
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

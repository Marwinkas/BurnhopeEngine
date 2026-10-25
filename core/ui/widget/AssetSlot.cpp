#include "ui/widget/AssetSlot.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/State.hpp"

namespace burnhope {

uint16_t uiAssetSlot(UiState& s, uint16_t parent, const UiAssetSlotConfig& cfg) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.gap = 4.0f;
    row.grow = 1.0f;
    row.shrink = 1.0f;
    row.align = UiAlign::Center;
    row.padL = 8.0f;
    row.padR = cfg.onClear != nullptr ? 36.0f : 8.0f;
    const uint16_t id = uiPanel(s, parent, row, paintToken(s.look, UiToken::Field, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    s.ent[id].add<UiFocusable>();
    uiHover(s, id);
    const char* shown = cfg.value != nullptr && cfg.value[0] != '\0' ? cfg.value : cfg.label;
    uiText(s, id, shown != nullptr ? shown : "");
    uiTextStyle(s, id, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (cfg.onClear != nullptr) {
        const uint16_t clearId = uiButton(s, id, {.label = "x", .width = 28.0f, .height = 24.0f, .onClick = cfg.onClear, .user = cfg.user});
        (void)clearId;
    }
    if (cfg.onBrowse != nullptr) {
        uiBind(s, id, cfg.onBrowse, cfg.user);
    }
    UiAssetSlot slot{};
    slot.caption = id;
    slot.onPath = cfg.onPath;
    slot.user = cfg.user;
    s.ent[id].set<UiAssetSlot>(slot);
    uiDragAddHost(s, id);
    return id;
}

void uiAssetSlotText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count || !s.ent[id].has<UiAssetSlot>()) {
        return;
    }
    uiText(s, id, text != nullptr && text[0] != '\0' ? text : "");
}

bool uiAssetOfferPath(UiState& s, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return false;
    }
    for (uint16_t id = s.hovered; id != kUiNone && id < s.count; id = s.parentOf[id]) {
        const UiAssetSlot* slot = s.ent[id].try_get<UiAssetSlot>();
        if (slot == nullptr || slot->onPath == nullptr) {
            continue;
        }
        uiText(s, id, path);
        slot->onPath(slot->user, path);
        return true;
    }
    return false;
}

} // namespace burnhope

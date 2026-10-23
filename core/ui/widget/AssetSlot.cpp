#include "ui/widget/AssetSlot.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Text.hpp"

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
    const uint16_t id = uiPanel(s, parent, row, paintToken(s.look, UiToken::Field, s.look.radiusSm));
    if (id == kUiNone) {
        return id;
    }
    s.ent[id].add<UiFocusable>();
    uiHover(s, id);
    UiFlex labelFlex{};
    labelFlex.grow = 1.0f;
    labelFlex.shrink = 1.0f;
    labelFlex.padL = 8.0f;
    const uint16_t capHost = uiPanel(s, id, labelFlex, paintToken(s.look, UiToken::Field, 0.0f));
    const char* shown = cfg.value != nullptr && cfg.value[0] != '\0' ? cfg.value : cfg.label;
    const uint16_t caption = uiLabel(s, capHost != kUiNone ? capHost : id, shown != nullptr ? shown : "");
    if (caption != kUiNone) {
        s.ent[caption].add<UiNoHit>();
    }
    if (cfg.onClear != nullptr) {
        const uint16_t clearId = uiButton(s, id, {.label = "x", .width = 28.0f, .height = 24.0f, .onClick = cfg.onClear, .user = cfg.user});
        (void)clearId;
    }
    if (cfg.onBrowse != nullptr) {
        uiBind(s, id, cfg.onBrowse, cfg.user);
    }
    UiAssetSlot slot{};
    slot.caption = caption;
    slot.onPath = cfg.onPath;
    slot.user = cfg.user;
    s.ent[id].set<UiAssetSlot>(slot);
    uiDragAddHost(s, id);
    return id;
}

void uiAssetSlotText(UiState& s, uint16_t id, const char* text) {
    if (id >= s.count) {
        return;
    }
    const UiAssetSlot* slot = s.ent[id].try_get<UiAssetSlot>();
    if (slot == nullptr || slot->caption == kUiNone) {
        return;
    }
    uiText(s, slot->caption, text != nullptr && text[0] != '\0' ? text : "");
}

} // namespace burnhope

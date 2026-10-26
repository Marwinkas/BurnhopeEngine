#include "ui/widget/AssetSlot.hpp"

#include "ui/Hover.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/Label.hpp"
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
    uiSetFocusable(s, id);
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

UiTexSlotIds uiTexSlot(UiState& s, uint16_t parent, const UiTexSlotConfig& cfg) {
    UiTexSlotIds out{};
    UiFlex frameFlex{};
    frameFlex.widthMode = UiSize::Px;
    frameFlex.heightMode = UiSize::Px;
    frameFlex.width = cfg.side;
    frameFlex.height = cfg.side;
    frameFlex.shrink = 0.0f;
    frameFlex.overflow = UiOverflow::Hidden;
    frameFlex.align = UiAlign::Center;
    frameFlex.justify = UiJustify::Center;
    out.frame = uiPanel(s, parent, frameFlex, paintToken(s.look, UiToken::Field, 4.0f));
    if (out.frame == kUiNone) {
        return out;
    }
    uiSetFocusable(s, out.frame);
    if (cfg.onBrowse != nullptr) {
        uiBind(s, out.frame, cfg.onBrowse, cfg.user);
    }
    s.ent[out.frame].set<UiAssetSlot>({out.frame, cfg.onPath, cfg.user});
    uiDragAddHost(s, out.frame);
    out.empty = uiLabel(s, out.frame, cfg.emptyLabel != nullptr ? cfg.emptyLabel : "");
    if (out.empty < s.count) {
        uiSetPass(s, out.empty);
        uiTextStyle(s, out.empty, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    }
    out.thumb = uiIcon(s, out.frame, cfg.photo, cfg.side - 8.0f, cfg.side - 8.0f);
    if (out.thumb < s.count) {
        uiSetPass(s, out.thumb);
        const uint8_t cols = cfg.atlasCols > 0 ? cfg.atlasCols : uint8_t{1};
        const uint8_t rows = cfg.atlasRows > 0 ? cfg.atlasRows : uint8_t{1};
        if (auto* image = s.ent[out.thumb].try_get_mut<UiImage>()) {
            image->viewW = 1.0f / static_cast<float>(cols);
            image->viewH = 1.0f / static_cast<float>(rows);
            image->viewX = static_cast<float>(cfg.cell % cols) * image->viewW;
            image->viewY = static_cast<float>(cfg.cell / cols) * image->viewH;
        }
        uiShow(s, out.thumb, false);
    }
    out.clear = uiButton(s, out.frame, {.label = "x", .width = 18.0f, .height = 18.0f, .onClick = cfg.onClear, .user = cfg.user});
    if (out.clear < s.count) {
        uiPlaceAbsolute(s, out.clear, cfg.side - 22.0f, 2.0f);
        if (auto* paint = s.ent[out.clear].try_get_mut<UiPaint>()) {
            *paint = paintToken(s.look, UiToken::Close, 2.0f);
        }
        uiShow(s, out.clear, false);
    }
    return out;
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

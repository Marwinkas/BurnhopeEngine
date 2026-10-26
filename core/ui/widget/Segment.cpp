#include "ui/widget/Segment.hpp"

#include "ui/Nodes.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/State.hpp"

namespace burnhope {
namespace {

void segmentClick(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    const UiSegmentItem* item = id < s.count ? s.ent[id].try_get<UiSegmentItem>() : nullptr;
    if (item == nullptr || item->strip >= s.count) {
        return;
    }
    const uint16_t strip = item->strip;
    const uint8_t index = item->index;
    uiSegmentSelect(s, strip, index);
    const UiSegment* seg = s.ent[strip].try_get<UiSegment>();
    if (seg != nullptr && seg->onPick != nullptr) {
        seg->onPick(seg->user, index);
    }
}

} // namespace

uint16_t uiSegment(UiState& s, uint16_t parent, const UiSegmentConfig& cfg) {
    if (cfg.labels == nullptr || cfg.count == 0) {
        return kUiNone;
    }
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.gap = 2.0f;
    line.shrink = 0.0f;
    line.heightMode = UiSize::Px;
    line.height = cfg.itemH;
    const uint16_t strip = uiPanel(s, parent, line, paintClear());
    if (strip >= s.count) {
        return strip;
    }
    const uint8_t selected = cfg.selected < cfg.count ? cfg.selected : 0;
    s.ent[strip].set<UiSegment>({.selected = selected, .onPick = cfg.onPick, .user = cfg.user});
    for (uint8_t i = 0; i < cfg.count; ++i) {
        const uint16_t id = uiButton(s, strip, {
            .label = cfg.labels[i],
            .width = cfg.itemW,
            .height = cfg.itemH,
            .onClick = segmentClick,
            .user = &s,
        });
        if (id < s.count) {
            s.ent[id].set<UiSegmentItem>({.strip = strip, .index = i});
            uiButtonMark(s, id, i == selected);
        }
    }
    return strip;
}

void uiSegmentSelect(UiState& s, uint16_t strip, uint8_t index) {
    auto* seg = strip < s.count ? s.ent[strip].try_get_mut<UiSegment>() : nullptr;
    if (seg == nullptr) {
        return;
    }
    seg->selected = index;
    const uint32_t n = uiNodeChildCount(s, strip);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t id = uiNodeChild(s, strip, i);
        const UiSegmentItem* item = id < s.count ? s.ent[id].try_get<UiSegmentItem>() : nullptr;
        if (item != nullptr) {
            uiButtonMark(s, id, item->index == index);
        }
    }
}

} // namespace burnhope

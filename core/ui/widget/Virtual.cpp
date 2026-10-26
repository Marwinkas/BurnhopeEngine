#include "ui/Hover.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/State.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

void onVirtClick(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    const UiVirtual* virt = uiAncestor<UiVirtual>(*s, id);
    if (virt == nullptr) {
        virt = s->ent[id].try_get<UiVirtual>();
    }
    const uint32_t index = uiVirtRowIndex(*s, id);
    if (virt != nullptr && index != 0xFFFFFFFFu && virt->click != nullptr) {
        virt->click(virt->user, index);
    }
}

uint16_t addVirtSlot(UiState& s, uint16_t scroll, float itemH) {
    UiFlex row{};
    row.position = UiPosition::Absolute;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    row.heightMode = UiSize::Px;
    row.height = itemH > 4.0f ? itemH - 4.0f : itemH;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    const uint16_t slot = spawn(s, scroll, row, paintToken(s.look, UiToken::Item, s.look.radiusSm));
    if (slot == kUiNone) {
        return slot;
    }
    addText(s, slot, " ");
    uiBind(s, slot, onVirtClick, &s);
    uiSetFocusable(s, slot);
    uiHover(s, slot);
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

bool uiPlaceOverSlot(UiState& s, uint16_t node, uint16_t slot, float fallbackH) {
    if (node >= s.count) {
        return false;
    }
    const UiParts* parts = slot < s.count ? s.ent[slot].try_get<UiParts>() : nullptr;
    const uint16_t lab = parts != nullptr && parts->label < s.count ? parts->label : slot;
    if (lab >= s.count) {
        uiShow(s, node, false);
        return false;
    }
    const UiBox& box = s.box[lab];
    if (box.w < 8.0f || box.h < 8.0f) {
        uiShow(s, node, false);
        return false;
    }
    const float w = box.w > 48.0f ? box.w : 160.0f;
    const float h = box.h > 16.0f ? box.h : fallbackH;
    const UiFlex* flex = s.ent[node].try_get<UiFlex>();
    const bool same = flex != nullptr && flex->posX == box.x && flex->posY == box.y && flex->width == w && flex->height == h
        && nodeShown(s, node);
    if (!same) {
        uiPlace(s, node, true, box.x, box.y, w, h);
        uiShow(s, node, true);
    }
    return true;
}

float uiVirtualTotalHeight(const UiState& s, uint16_t id, float itemH, uint32_t count, uint8_t cols) {
    const uint8_t columns = cols < 1 ? 1 : cols;
    const float h = id < s.count ? s.box[id].h : 0.0f;
    return uiVirtualCalc(0.0f, h, itemH, count, columns).totalHeight;
}

float uiVirtualContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight(s, id, id < s.count ? s.ent[id].try_get<UiVirtual>() : nullptr);
}

void virtualOnLayout(UiState& s, uint16_t id, void*);

uint16_t uiVirtual(UiState& s, uint16_t parent, float itemH) {
    if (itemH < 8.0f) {
        itemH = 36.0f;
    }
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiToken::Content, s.look.radius);
    if (scroll == kUiNone) {
        return scroll;
    }
    if (auto* host = s.ent[scroll].try_get_mut<UiFlex>()) {
        host->overflow = UiOverflow::Hidden;
    }
    const UiScroll* scrollRow = s.ent[scroll].try_get<UiScroll>();
    if (scrollRow != nullptr) {
        if (auto* spacer = scrollRow->inner < s.count ? s.ent[scrollRow->inner].try_get_mut<UiFlex>() : nullptr) {
            spacer->position = UiPosition::Absolute;
        }
        uiVirtualSyncSpacer(s, scrollRow->inner, itemH);
    }
    UiVirtual virt{};
    virt.itemH = itemH;
    s.ent[scroll].set<UiVirtual>(virt);
    uiVirtualAttach(s, scroll, uiVirtualRefresh, uiVirtualContentH, 0);
    s.ent[scroll].set<UiLayoutRefresh>({virtualOnLayout, nullptr});
    return scroll;
}

void uiVirtualRefresh(UiState& s, uint16_t id) {
    auto* virt = id < s.count ? s.ent[id].try_get_mut<UiVirtual>() : nullptr;
    auto* scroll = virt != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (virt == nullptr || scroll == nullptr) {
        return;
    }
    const uint16_t need = uiVirtualSlots(s.box[id].h, virt->itemH, 1);
    uiVirtualEnsure(s, *virt, need, 2, [&] { return addVirtSlot(s, id, virt->itemH); });
    const UiVirtualWindow win = uiVirtualCalc(scroll->offset, s.box[id].h, virt->itemH, virt->count, 1);
    virt->first = win.first;
    uiVirtualSyncSpacer(s, scroll->inner, win.totalHeight);
    for (uint8_t i = 0; i < virt->slots; ++i) {
        const uint16_t slot = virt->slot[i];
        const uint32_t index = win.first + i;
        const bool on = i < win.visibleCount && slot < s.count;
        uiVirtualSetShown(s, slot, on);
        if (!on || slot >= s.count) {
            continue;
        }
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = UiPosition::Absolute;
            flex->posY = uiVirtualSlotY(index, virt->itemH, scroll->offset);
            flex->height = virt->itemH > 4.0f ? virt->itemH - 4.0f : virt->itemH;
        }
        s.ent[slot].set<UiVirtRow>({index});
        char text[96]{};
        if (virt->fill != nullptr) {
            virt->fill(virt->user, index, text, static_cast<uint8_t>(sizeof(text) - 1u));
        } else {
            std::snprintf(text, sizeof(text), "%u", index);
        }
        uiText(s, slot, text);
    }
    s.visualDirty = true;
}

void virtualOnLayout(UiState& s, uint16_t id, void*) {
    uiVirtualRefresh(s, id);
}

void uiVirtualSource(
    UiState& s,
    uint16_t id,
    uint32_t count,
    void (*fill)(void*, uint32_t, char*, uint8_t),
    void (*click)(void*, uint32_t),
    void* user) {
    auto* virt = id < s.count ? s.ent[id].try_get_mut<UiVirtual>() : nullptr;
    if (virt == nullptr) {
        return;
    }
    virt->count = count;
    virt->fill = fill;
    virt->click = click;
    virt->user = user;
    uiVirtualRefresh(s, id);
    uiMarkLayout(s, id);
}

void uiVirtualAttach(
    UiState& s,
    uint16_t id,
    void (*refresh)(UiState& s, uint16_t id),
    float (*contentHeight)(const UiState& s, uint16_t id),
    uint8_t follow) {
    if (auto* scroll = id < s.count ? s.ent[id].try_get_mut<UiScroll>() : nullptr) {
        scroll->contentHeight = contentHeight;
        scroll->onScroll = refresh;
        scroll->follow = follow;
    }
    s.ent[id].set<UiVirtualPool>({refresh, contentHeight, follow});
}

uint16_t uiVirtualHitRow(const UiState& s, uint16_t host, uint32_t& index) {
    index = 0xFFFFFFFFu;
    if (host >= s.count || (s.hovered != host && !uiWithin(s, s.hovered, host))) {
        return kUiNone;
    }
    for (uint16_t p = s.hovered; p != kUiNone && p < s.count && p != host; p = s.parentOf[p]) {
        const UiVirtRow* row = s.ent[p].try_get<UiVirtRow>();
        if (row != nullptr) {
            index = row->index;
            return p;
        }
    }
    return kUiNone;
}

uint32_t uiVirtRowIndex(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return 0xFFFFFFFFu;
    }
    const UiVirtRow* row = s.ent[id].try_get<UiVirtRow>();
    if (row == nullptr) {
        for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            row = s.ent[p].try_get<UiVirtRow>();
            if (row != nullptr) {
                break;
            }
        }
    }
    return row != nullptr ? row->index : 0xFFFFFFFFu;
}

uint16_t uiVirtualHostCreate(UiState& s, uint16_t parent, UiToken token, float radius) {
    UiFlex host{};
    host.grow = 1.0f;
    host.shrink = 1.0f;
    host.overflow = UiOverflow::Scroll;
    host.position = UiPosition::Relative;
    host.justify = UiJustify::Start;
    host.align = UiAlign::Stretch;
    host.content = UiAlignContent::Start;
    const uint16_t scroll = spawn(s, parent, host, paintToken(s.look, token, radius));
    if (scroll == kUiNone) {
        return kUiNone;
    }
    UiFlex spacer{};
    spacer.widthMode = UiSize::Percent;
    spacer.width = 100.0f;
    spacer.heightMode = UiSize::Px;
    spacer.height = 1.0f;
    spacer.shrink = 0.0f;
    spacer.position = UiPosition::Absolute;
    spacer.posX = 0.0f;
    spacer.posY = 0.0f;
    const uint16_t inner = spawn(s, scroll, spacer, paintClear());
    s.ent[scroll].set<UiScroll>({0.0f, inner});
    return scroll;
}

void uiVirtualSyncSpacer(UiState& s, uint16_t inner, float totalHeight) {
    if (inner >= s.count) {
        return;
    }
    if (auto* flex = s.ent[inner].try_get_mut<UiFlex>()) {
        flex->height = totalHeight;
        flex->position = UiPosition::Absolute;
        flex->posX = 0.0f;
        flex->posY = 0.0f;
        flex->marginT = 0.0f;
        flex->marginB = 0.0f;
    }
}

bool uiVirtualRoom(const UiState& s, uint16_t budget) {
    return s.count + budget < kUiCap;
}

} // namespace burnhope

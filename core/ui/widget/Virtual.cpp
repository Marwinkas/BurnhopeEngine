#include "ui/widget/Detail.hpp"
#include "ui/widget/Virtual.hpp"

#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

void onVirtClick(void* user, uint16_t id, int16_t) {
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
    row.position = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Percent);
    row.width = 100.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = itemH > 4.0f ? itemH - 4.0f : itemH;
    row.shrink = 0.0f;
    row.align = 1;
    const uint16_t slot = spawn(s, scroll, row, paintToken(s.look, UiRole::Button, UiToken::Item, s.look.radiusSm));
    if (slot == kUiNone) {
        return slot;
    }
    addText(s, slot, " ");
    uiBind(s, slot, onVirtClick, &s);
    s.ent[slot].set<UiVirtRow>({});
    uiShow(s, slot, false);
    return slot;
}

} // namespace

float uiVirtualTotalHeight(const UiState& s, uint16_t id, float itemH, uint32_t count, uint8_t cols) {
    const uint8_t columns = cols < 1 ? 1 : cols;
    const float h = id < s.count ? s.box[id].h : 0.0f;
    return uiVirtualCalc(0.0f, h, itemH, count, columns).totalHeight;
}

float uiVirtualContentH(const UiState& s, uint16_t id) {
    return uiVirtualContentHeight<UiVirtual>(s, id);
}

uint16_t uiVirtual(UiState& s, uint16_t parent, float itemH) {
    if (itemH < 8.0f) {
        itemH = 36.0f;
    }
    const uint16_t scroll = uiVirtualHostCreate(s, parent, UiRole::Scroll, UiToken::Content, s.look.radius);
    if (scroll == kUiNone) {
        return scroll;
    }
    if (auto* host = s.ent[scroll].try_get_mut<UiFlex>()) {
        host->overflow = 1;
    }
    const UiScroll* scrollRow = s.ent[scroll].try_get<UiScroll>();
    if (scrollRow != nullptr) {
        if (auto* spacer = scrollRow->inner < s.count ? s.ent[scrollRow->inner].try_get_mut<UiFlex>() : nullptr) {
            spacer->position = 1;
        }
        uiVirtualSyncSpacer(s, scrollRow->inner, itemH);
    }
    UiVirtual virt{};
    virt.itemH = itemH;
    s.ent[scroll].set<UiVirtual>(virt);
    s.ent[scroll].set<UiVirtualPool>({uiVirtualRefresh, uiVirtualContentH, 0});
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
            flex->position = 1;
            flex->posY = static_cast<float>(i) * virt->itemH;
            flex->height = virt->itemH > 4.0f ? virt->itemH - 4.0f : virt->itemH;
        }
        s.ent[slot].set<UiVirtRow>({index});
        char text[32]{};
        if (virt->fill != nullptr) {
            virt->fill(virt->user, index, text, 31);
        } else {
            std::snprintf(text, sizeof(text), "%u", index);
        }
        uiText(s, slot, text);
    }
    s.visualDirty = true;
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

} // namespace burnhope

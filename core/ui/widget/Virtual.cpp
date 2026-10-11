#include "ui/widget/Detail.hpp"

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
    if (virt == nullptr && id < s->count) {
        virt = s->ent[id].try_get<UiVirtual>();
    }
    const UiVirtRow* row = s->ent[id].try_get<UiVirtRow>();
    if (row == nullptr) {
        row = uiAncestor<UiVirtRow>(*s, id);
    }
    if (virt != nullptr && row != nullptr && virt->click != nullptr) {
        virt->click(virt->user, row->index);
    }
}

uint16_t addVirtSlot(UiState& s, uint16_t scroll, float rowH) {
    UiFlex row{};
    row.position = 1;
    row.widthMode = static_cast<uint8_t>(UiSize::Percent);
    row.width = 100.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = rowH > 4.0f ? rowH - 4.0f : rowH;
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

uint16_t uiVirtual(UiState& s, uint16_t parent, float rowH) {
    if (rowH < 8.0f) {
        rowH = 36.0f;
    }
    UiFlex host{};
    host.grow = 1.0f;
    host.shrink = 1.0f;
    host.overflow = 1;
    host.position = 2;
    const uint16_t scroll = spawn(s, parent, host, paintToken(s.look, UiRole::Scroll, UiToken::Content, s.look.radius));
    UiFlex spacer{};
    spacer.position = 1;
    spacer.widthMode = static_cast<uint8_t>(UiSize::Percent);
    spacer.width = 100.0f;
    spacer.heightMode = static_cast<uint8_t>(UiSize::Px);
    spacer.height = rowH;
    const uint16_t inner = spawn(s, scroll, spacer, paint(UiRole::Hidden, 0, 0, 0));
    s.ent[scroll].set<UiScroll>({0.0f, inner});
    UiVirtual virt{};
    virt.rowH = rowH;
    s.ent[scroll].set<UiVirtual>(virt);
    s.ent[scroll].set<UiVirtualPool>({uiVirtualRefresh});
    return scroll;
}

void uiVirtualRefresh(UiState& s, uint16_t id) {
    auto* virt = id < s.count ? s.ent[id].try_get_mut<UiVirtual>() : nullptr;
    auto* scroll = virt != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (virt == nullptr || scroll == nullptr) {
        return;
    }
    const uint16_t need = uiVirtualSlots(s.box[id].h, virt->rowH, 1);
    while (virt->slots < need && virt->slots < kUiPoolCap && s.count + 2 < kUiCap) {
        const uint16_t slot = addVirtSlot(s, id, virt->rowH);
        if (slot == kUiNone) {
            break;
        }
        virt->slot[virt->slots++] = slot;
    }
    const UiVirtualWindow win = uiVirtualCalc(scroll->offset, s.box[id].h, virt->rowH, virt->count, 1);
    virt->first = win.first;
    if (scroll->inner < s.count) {
        if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
            flex->height = win.totalHeight;
        }
    }
    for (uint8_t i = 0; i < virt->slots; ++i) {
        const uint16_t slot = virt->slot[i];
        const uint32_t index = win.first + i;
        const bool on = i < win.visibleCount && slot < s.count;
        const bool live = slot < s.count && s.yoga[slot] != nullptr && YGNodeStyleGetDisplay(s.yoga[slot]) != YGDisplayNone;
        if (live != on) {
            uiShow(s, slot, on);
        }
        if (!on || slot >= s.count) {
            continue;
        }
        if (auto* flex = s.ent[slot].try_get_mut<UiFlex>()) {
            flex->position = 1;
            flex->posY = static_cast<float>(i) * virt->rowH;
            flex->height = virt->rowH > 4.0f ? virt->rowH - 4.0f : virt->rowH;
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

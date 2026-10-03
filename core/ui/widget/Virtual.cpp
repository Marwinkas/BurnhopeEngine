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
    const uint16_t host = s->parentOf[id];
    const UiVirtual* virt = host < s->count ? s->ent[host].try_get<UiVirtual>() : nullptr;
    const UiVirtRow* row = s->ent[id].try_get<UiVirtRow>();
    if (virt != nullptr && row != nullptr && virt->click != nullptr) {
        virt->click(virt->user, row->index);
    }
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
    const uint16_t scroll = spawn(s, parent, host, paint(UiRole::Scroll, 0.07f, 0.075f, 0.09f, 8.0f));
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
    virt.slots = 12;
    for (uint8_t i = 0; i < virt.slots; ++i) {
        UiFlex row{};
        row.position = 1;
        row.posY = static_cast<float>(i) * rowH;
        row.widthMode = static_cast<uint8_t>(UiSize::Percent);
        row.width = 100.0f;
        row.heightMode = static_cast<uint8_t>(UiSize::Px);
        row.height = rowH - 4.0f;
        row.shrink = 0.0f;
        row.align = 1;
        const uint16_t slot = spawn(s, scroll, row, paint(UiRole::Button, 0.16f, 0.17f, 0.21f, 6.0f));
        addText(s, slot, " ");
        uiBind(s, slot, onVirtClick, &s);
        s.ent[slot].set<UiVirtRow>({});
        virt.slot[i] = slot;
    }
    s.ent[scroll].set<UiVirtual>(virt);
    return scroll;
}

void uiVirtualRefresh(UiState& s, uint16_t id) {
    auto* virt = id < s.count ? s.ent[id].try_get_mut<UiVirtual>() : nullptr;
    auto* scroll = virt != nullptr ? s.ent[id].try_get<UiScroll>() : nullptr;
    if (virt == nullptr || scroll == nullptr) {
        return;
    }
    uint32_t first = virt->rowH > 1.0f ? static_cast<uint32_t>(scroll->offset / virt->rowH) : 0;
    if (virt->count > virt->slots && first + virt->slots > virt->count) {
        first = virt->count - virt->slots;
    }
    if (virt->count <= virt->slots) {
        first = 0;
    }
    virt->first = first;
    if (scroll->inner < s.count) {
        if (auto* flex = s.ent[scroll->inner].try_get_mut<UiFlex>()) {
            flex->height = virt->count * virt->rowH;
        }
    }
    for (uint8_t i = 0; i < virt->slots; ++i) {
        const uint16_t slot = virt->slot[i];
        const uint32_t index = first + i;
        const bool on = index < virt->count;
        uiShow(s, slot, on);
        if (!on || slot >= s.count) {
            continue;
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

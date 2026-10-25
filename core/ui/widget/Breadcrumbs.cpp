#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/State.hpp"

namespace burnhope {
namespace {

uint16_t crumbAt(const UiState& s, uint16_t bar, uint32_t index) {
    return uiNodeChild(s, bar, index);
}

void onSeg(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    const uint16_t bar = uiAncestorId<UiCrumbs>(*s, id);
    const UiCrumbs* crumbs = bar < s->count ? s->ent[bar].try_get<UiCrumbs>() : nullptr;
    if (crumbs == nullptr || crumbs->nav == nullptr) {
        return;
    }
    uint16_t index = 0;
    const uint32_t kids = uiNodeChildCount(*s, bar);
    for (uint32_t i = 0; i < kids; i += 2) {
        const uint16_t seg = crumbAt(*s, bar, i);
        if (seg >= s->count) {
            break;
        }
        if (seg == id) {
            crumbs->nav(crumbs->user, index);
            return;
        }
        ++index;
    }
}

} // namespace

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, uint16_t index), void* user) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.grow = 1.0f;
    row.shrink = 1.0f;
    row.overflow = UiOverflow::Visible;
    row.align = UiAlign::Center;
    row.justify = UiJustify::Start;
    row.gap = 2.0f;
    row.padL = 4.0f;
    row.padR = 4.0f;
    row.widthMode = UiSize::Auto;
    row.heightMode = UiSize::Px;
    row.height = 30.0f;
    const uint16_t id = spawn(s, parent, row, paintLabel(s.look));
    UiCrumbs crumbs{};
    crumbs.nav = nav;
    crumbs.user = user;
    s.ent[id].set<UiCrumbs>(crumbs);
    return id;
}

void uiBindCrumbs(UiState& s, const char* name, void (*nav)(void* user, uint16_t index), void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* crumbs = id < s.count ? s.ent[id].try_get_mut<UiCrumbs>() : nullptr;
    if (crumbs == nullptr) {
        return;
    }
    crumbs->nav = nav;
    crumbs->user = user;
}

void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* const* labels, uint8_t count) {
    auto* crumbs = id < s.count ? s.ent[id].try_get_mut<UiCrumbs>() : nullptr;
    if (crumbs == nullptr) {
        return;
    }
    auto grow = [&]() {
        if (s.count + 2 >= kUiCap) {
            return;
        }
        const uint16_t button = uiButton(s, id, UiButtonConfig{.padX = 4.0f, .onClick = onSeg, .user = &s});
        if (button != kUiNone) {
            if (auto* paint = s.ent[button].try_get_mut<UiPaint>()) {
                *paint = paintLabel(s.look);
            }
            uiShow(s, button, false);
        }
        const UiPaint arrow = paintLabel(s.look);
        UiFlex sepFlex = px(10.0f, 14.0f);
        sepFlex.align = UiAlign::Center;
        const uint16_t sep = spawn(s, id, sepFlex, arrow);
        addText(s, sep, ">");
        uiShow(s, sep, false);
    };
    uint8_t seg = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const char* label = labels != nullptr ? labels[i] : nullptr;
        if (label == nullptr || label[0] == '\0') {
            continue;
        }
        const uint32_t slots = uiNodeChildCount(s, id) / 2;
        if (seg >= slots) {
            grow();
        }
        if (seg >= uiNodeChildCount(s, id) / 2) {
            break;
        }
        char piece[32]{};
        uint8_t len = 0;
        for (const char* p = label; *p != '\0' && len < 31; ++p) {
            piece[len++] = *p;
        }
        piece[len] = '\0';
        const uint16_t button = crumbAt(s, id, static_cast<uint32_t>(seg) * 2u);
        const uint16_t sep = seg > 0 ? crumbAt(s, id, static_cast<uint32_t>(seg) * 2u - 1u) : kUiNone;
        if (button >= s.count) {
            break;
        }
        uiText(s, button, piece);
        if (auto* flex = s.ent[button].try_get_mut<UiFlex>()) {
            flex->width = uiMeasure(s, piece, len, len) + 8.0f;
            flex->height = 22.0f;
        }
        uiShow(s, button, true);
        if (sep < s.count) {
            uiShow(s, sep, true);
        }
        ++seg;
    }
    crumbs->n = seg;
    const uint32_t slots = uiNodeChildCount(s, id) / 2;
    for (uint32_t i = seg; i < slots; ++i) {
        const uint16_t button = crumbAt(s, id, i * 2u);
        if (button < s.count) {
            uiShow(s, button, false);
        }
        const uint16_t sep = crumbAt(s, id, i * 2u + 1u);
        if (sep < s.count) {
            uiShow(s, sep, false);
        }
    }
}

uint16_t loadCrumbs(UiState& s, uint16_t parent, const BinNode&, const BinTail&, const char*, const char*, const char*, void*) {
    return uiBreadcrumbs(s, parent, nullptr, nullptr);
}

void uiCrumbsBin(UiState& s) {
    uiBinRole(s, kBinSlotCrumbs, "crumbs", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotCrumbs, loadCrumbs, nullptr);
}

} // namespace burnhope

#include "ui/widget/Detail.hpp"

#include <cstring>

namespace burnhope {
namespace {

void onSeg(void* user, uint16_t id, int16_t) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    const UiCrumbs* crumbs = uiAncestor<UiCrumbs>(*s, id);
    if (crumbs == nullptr || crumbs->nav == nullptr) {
        return;
    }
    char path[512]{};
    path[0] = '/';
    int n = 1;
    for (uint8_t i = 0; i < crumbs->n; ++i) {
        if (crumbs->seg[i] >= s->count) {
            break;
        }
        const UiText* text = s->ent[crumbs->seg[i]].try_get<UiText>();
        if (text == nullptr || text->len == 0) {
            continue;
        }
        if (n > 1 && n < 511) {
            path[n++] = '/';
        }
        for (uint8_t c = 0; c < text->len && n < 511; ++c) {
            path[n++] = text->bytes[c];
        }
        path[n] = '\0';
        if (crumbs->seg[i] == id) {
            crumbs->nav(crumbs->user, path);
            return;
        }
    }
}

} // namespace

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user) {
    UiFlex row{};
    row.direction = 1;
    row.grow = 1.0f;
    row.shrink = 1.0f;
    row.align = 1;
    row.gap = 4.0f;
    row.heightMode = static_cast<uint8_t>(UiSize::Px);
    row.height = 28.0f;
    const uint16_t id = spawn(s, parent, row, paint(UiRole::Hidden, 0, 0, 0));
    UiCrumbs crumbs{};
    crumbs.nav = nav;
    crumbs.user = user;
    for (uint8_t i = 0; i < 8; ++i) {
        crumbs.seg[i] = uiButton(s, id, "", onSeg, &s);
        if (crumbs.seg[i] != kUiNone) {
            uiShow(s, crumbs.seg[i], false);
        }
        if (i < 7) {
            crumbs.sep[i] = spawn(s, id, px(12.0f, 16.0f), paintToken(s.look, UiRole::Label, UiToken::Muted));
            addText(s, crumbs.sep[i], ">");
            uiShow(s, crumbs.sep[i], false);
        }
    }
    s.ent[id].set<UiCrumbs>(crumbs);
    return id;
}

void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path) {
    auto* crumbs = id < s.count ? s.ent[id].try_get_mut<UiCrumbs>() : nullptr;
    if (crumbs == nullptr) {
        return;
    }
    char local[512]{};
    int n = 0;
    if (path != nullptr) {
        while (path[n] != '\0' && n < 511) {
            local[n] = path[n];
            ++n;
        }
    }
    local[n] = '\0';
    std::memcpy(crumbs->path, local, n + 1);
    uint8_t seg = 0;
    uint8_t at = local[0] == '/' ? 1 : 0;
    while (at <= n && seg < 8) {
        uint8_t end = at;
        while (local[end] != '\0' && local[end] != '/') {
            ++end;
        }
        char piece[32]{};
        uint8_t len = 0;
        for (uint8_t i = at; i < end && len < 31; ++i) {
            piece[len++] = local[i];
        }
        piece[len] = '\0';
        if (len > 0 && crumbs->seg[seg] < s.count) {
            uiText(s, crumbs->seg[seg], piece);
            if (auto* flex = s.ent[crumbs->seg[seg]].try_get_mut<UiFlex>()) {
                flex->width = 12.0f + static_cast<float>(len) * 8.0f;
            }
            uiShow(s, crumbs->seg[seg], true);
            if (seg > 0 && crumbs->sep[seg - 1] < s.count) {
                uiShow(s, crumbs->sep[seg - 1], true);
            }
            ++seg;
        }
        at = local[end] == '/' ? static_cast<uint8_t>(end + 1) : end;
        if (local[end] == '\0') {
            break;
        }
    }
    crumbs->n = seg;
    for (uint8_t i = seg; i < 8; ++i) {
        if (crumbs->seg[i] < s.count) {
            uiShow(s, crumbs->seg[i], false);
        }
    }
    for (uint8_t i = 0; i < 7; ++i) {
        if (crumbs->sep[i] < s.count && (seg == 0 || i >= seg - 1)) {
            uiShow(s, crumbs->sep[i], false);
        }
    }
}

} // namespace burnhope

#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Detail.hpp"

#include <cstring>

namespace burnhope {
namespace {

void onSeg(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    const UiCrumbs* crumbs = uiAncestor<UiCrumbs>(*s, id);
    if (crumbs == nullptr || crumbs->nav == nullptr) {
        return;
    }
    const bool rooted = crumbs->root[0] != '\0';
    char rel[512]{};
    rel[0] = '\0';
    int relN = 0;
    for (uint8_t i = 0; i < crumbs->n; ++i) {
        if (crumbs->seg[i] >= s->count) {
            break;
        }
        if (rooted && i == 0) {
            if (crumbs->seg[i] == id) {
                crumbs->nav(crumbs->user, crumbs->root);
                return;
            }
            continue;
        }
        const UiText* text = s->ent[crumbs->seg[i]].try_get<UiText>();
        if (text == nullptr || text->len == 0) {
            continue;
        }
        if (relN > 0 && relN < 511) {
            rel[relN++] = '/';
        }
        for (uint8_t c = 0; c < text->len && relN < 511; ++c) {
            rel[relN++] = text->bytes[c];
        }
        rel[relN] = '\0';
        if (crumbs->seg[i] != id) {
            continue;
        }
        char path[512]{};
        if (rooted) {
            int n = 0;
            for (const char* p = crumbs->root; *p != '\0' && n < 511; ++p) {
                path[n++] = *p;
            }
            if (relN > 0) {
                if (n > 0 && path[n - 1] != '/' && n < 511) {
                    path[n++] = '/';
                }
                for (int r = 0; rel[r] != '\0' && n < 511; ++r) {
                    path[n++] = rel[r];
                }
            }
            path[n] = '\0';
        } else {
            path[0] = '/';
            int n = 1;
            for (int r = 0; rel[r] != '\0' && n < 511; ++r) {
                path[n++] = rel[r];
            }
            path[n] = '\0';
        }
        crumbs->nav(crumbs->user, path);
        return;
    }
}

} // namespace

uint16_t uiBreadcrumbs(UiState& s, uint16_t parent, void (*nav)(void* user, const char* path), void* user) {
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
    UiPaint bar = paintLabel(s.look);
    const uint16_t id = spawn(s, parent, row, bar);
    UiCrumbs crumbs{};
    crumbs.nav = nav;
    crumbs.user = user;
    s.ent[id].set<UiCrumbs>(crumbs);
    return id;
}

void uiBindCrumbs(UiState& s, const char* name, void (*nav)(void* user, const char* path), void* user) {
    const uint16_t id = uiFindName(s, name);
    auto* crumbs = id < s.count ? s.ent[id].try_get_mut<UiCrumbs>() : nullptr;
    if (crumbs == nullptr) {
        return;
    }
    crumbs->nav = nav;
    crumbs->user = user;
}

void uiBreadcrumbsSet(UiState& s, uint16_t id, const char* path, const char* root) {
    auto* crumbs = id < s.count ? s.ent[id].try_get_mut<UiCrumbs>() : nullptr;
    if (crumbs == nullptr) {
        return;
    }
    crumbs->root[0] = '\0';
    if (root != nullptr && root[0] != '\0') {
        copyCap(crumbs->root, 512, root);
    }
    char local[512]{};
    if (root != nullptr && root[0] != '\0' && path != nullptr) {
        const size_t rootLen = std::strlen(root);
        if (std::strncmp(path, root, rootLen) == 0) {
            const char* tail = path + rootLen;
            if (tail[0] == '/') {
                ++tail;
            }
            const char* leaf = std::strrchr(root, '/');
            leaf = leaf != nullptr && leaf[1] != '\0' ? leaf + 1 : root;
            if (tail[0] == '\0') {
                copyCap(local, 512, leaf);
            } else {
                char joined[512]{};
                int n = 0;
                for (const char* p = leaf; *p != '\0' && n < 510; ++p) {
                    joined[n++] = *p;
                }
                joined[n++] = '/';
                for (const char* p = tail; *p != '\0' && n < 511; ++p) {
                    joined[n++] = *p;
                }
                joined[n] = '\0';
                copyCap(local, 512, joined);
            }
        } else {
            copyCap(local, 512, path);
        }
    } else {
        copyCap(local, 512, path);
    }
    std::memcpy(crumbs->path, local, std::strlen(local) + 1u);
    auto grow = [&]() {
        if (crumbs->cap >= kUiCrumbCap || s.count + 2 >= kUiCap) {
            return;
        }
        const uint8_t i = crumbs->cap;
        crumbs->seg[i] = uiButton(s, id, UiButtonConfig{ .onClick = onSeg, .user = &s });
        if (crumbs->seg[i] != kUiNone) {
            if (auto* paint = s.ent[crumbs->seg[i]].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
            }
            if (auto* flex = s.ent[crumbs->seg[i]].try_get_mut<UiFlex>()) {
                flex->padL = 4.0f;
                flex->padR = 4.0f;
            }
            uiShow(s, crumbs->seg[i], false);
        }
        UiPaint arrow = paintLabel(s.look);
        arrow.shadow = 0.0f;
        crumbs->sep[i] = spawn(s, id, px(10.0f, 14.0f), arrow);
        addText(s, crumbs->sep[i], ">");
        if (crumbs->sep[i] < s.count) {
            if (auto* sepFlex = s.ent[crumbs->sep[i]].try_get_mut<UiFlex>()) {
                sepFlex->align = UiAlign::Center;
            }
        }
        uiShow(s, crumbs->sep[i], false);
        crumbs->cap = static_cast<uint8_t>(i + 1);
    };
    uint8_t seg = 0;
    uint8_t at = 0;
    const uint8_t n = uiStrCopy(local, local);
    while (at <= n && seg < kUiCrumbCap) {
        if (seg >= crumbs->cap) {
            grow();
        }
        if (seg >= crumbs->cap) {
            break;
        }
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
                flex->width = uiMeasure(s, piece, len, len) + 8.0f;
                flex->height = 22.0f;
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
    for (uint8_t i = seg; i < crumbs->cap; ++i) {
        if (crumbs->seg[i] < s.count) {
            uiShow(s, crumbs->seg[i], false);
        }
    }
    for (uint8_t i = 0; i < crumbs->cap; ++i) {
        if (crumbs->sep[i] < s.count && (seg == 0 || i >= seg - 1)) {
            uiShow(s, crumbs->sep[i], false);
        }
    }
}

} // namespace burnhope

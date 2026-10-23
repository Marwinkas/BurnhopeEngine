#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Fold.hpp"

namespace burnhope {
namespace {

void tickFoldMarks(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree == nullptr) {
            continue;
        }
        const uint16_t clip = tree->clip;
        if (tree->open != 0 && clip < s.count && tree->body < s.count && s.box[tree->body].h > 8.0f) {
            const UiReveal* rev = s.ent[clip].try_get<UiReveal>();
            if (rev == nullptr || rev->t0 < 0.0f) {
                tree->full = s.box[tree->body].h;
            }
        }
        float angle = tree->open != 0 ? 90.0f : 0.0f;
        if (clip < s.count) {
            const UiReveal* rev = s.ent[clip].try_get<UiReveal>();
            if (rev != nullptr && rev->t0 >= 0.0f) {
                const float u = uiRevealUnit(s, clip);
                angle = (rev->to > rev->from ? u : 1.0f - u) * 90.0f;
            }
        }
        auto* paint = s.ent[id].try_get_mut<UiPaint>();
        if (paint != nullptr && paint->angle != angle) {
            paint->angle = angle;
            uiNotifyVisualDirty(s);
        }
    }
}

void onTree(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* tree = s->ent[id].try_get_mut<UiTree>();
    if (tree == nullptr || tree->clip >= s->count) {
        return;
    }
    const bool opening = tree->open == 0;
    tree->open = opening ? 1 : 0;
    float full = tree->full;
    if (opening && full < 8.0f && tree->body < s->count && s->box[tree->body].h > 8.0f) {
        full = s->box[tree->body].h;
        tree->full = full;
    }
    if (full < 8.0f) {
        full = 28.0f;
    }
    uiShow(*s, tree->body, true);
    uiReveal(*s, tree->clip, full, opening);
}

} // namespace

uint16_t uiTreeRow(UiState& s, uint16_t parent, const char* text) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.position = UiPosition::Relative;
    row.heightMode = UiSize::Px;
    row.height = 28.0f;
    row.shrink = 0.0f;
    row.align = UiAlign::Center;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    row.padL = 8.0f;
    row.padR = 22.0f;
    if (parent < s.count) {
        if (const UiFlex* home = s.ent[parent].try_get<UiFlex>()) {
            const float left = home->pad > 0.0f ? home->pad : home->padL;
            const float right = home->pad > 0.0f ? home->pad : home->padR;
            if (left > 0.0f) {
                row.marginL = -left;
            }
            if (right > 0.0f) {
                row.marginR = -right;
            }
        }
    }
    UiPaint bar = paintToken(s.look, UiToken::Item, 0.0f);
    bar.icon = static_cast<uint8_t>(UiIcon::ChevronRight);
    const uint16_t head = uiPanel(s, parent, row, bar);
    if (head == kUiNone) {
        return kUiNone;
    }
    uiText(s, head, text != nullptr ? text : "");
    uiTextStyle(s, head, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiHover(s, head);
    s.ent[head].add<UiFocusable>();
    UiFlex clipFlex{};
    clipFlex.direction = UiFlexDir::Column;
    clipFlex.shrink = 0.0f;
    clipFlex.widthMode = UiSize::Percent;
    clipFlex.width = 100.0f;
    clipFlex.heightMode = UiSize::Px;
    clipFlex.height = 0.0f;
    clipFlex.minH = 0.0f;
    clipFlex.overflow = UiOverflow::Hidden;
    const uint16_t clip = uiPanel(s, parent, clipFlex, paintClear());
    UiFlex body{};
    body.direction = UiFlexDir::Column;
    body.shrink = 0.0f;
    body.widthMode = UiSize::Percent;
    body.width = 100.0f;
    body.pad = 4.0f;
    body.gap = 2.0f;
    const uint16_t child = uiPanel(s, clip, body, paintToken(s.look, UiToken::Item, s.look.radiusSm));
    UiTree tree{};
    tree.body = child;
    tree.clip = clip;
    s.ent[head].set<UiTree>(tree);
    uiBind(s, head, onTree, &s);
    uiBeforeLayoutAdd(s, tickFoldMarks);
    return child;
}

void uiFoldOpen(UiState& s, uint16_t body) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree == nullptr || tree->body != body) {
            continue;
        }
        tree->open = 1;
        uiShow(s, body, true);
        if (tree->clip < s.count) {
            if (auto* flex = s.ent[tree->clip].try_get_mut<UiFlex>()) {
                flex->heightMode = UiSize::Auto;
                flex->height = 0.0f;
                flex->overflow = UiOverflow::Hidden;
            }
        }
        if (auto* paint = s.ent[id].try_get_mut<UiPaint>()) {
            paint->icon = static_cast<uint8_t>(UiIcon::ChevronRight);
            paint->angle = 90.0f;
        }
        break;
    }
}

uint16_t uiFoldBody(UiState& s, uint16_t parent, const char* title) {
    const uint16_t body = uiTreeRow(s, parent, title);
    uiFoldOpen(s, body);
    return body;
}

void uiFold(UiState& s, uint16_t id) {
    const uint16_t header = s.parentOf[id];
    const uint16_t panel = header == kUiNone ? kUiNone : s.parentOf[header];
    if (panel == kUiNone || panel >= s.count) {
        return;
    }
    bool open = false;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != panel || child == header) {
            continue;
        }
        if (nodeShown(s, child)) {
            open = true;
        }
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == panel && child != header) {
            uiShow(s, child, !open);
        }
    }
    auto* flex = s.ent[panel].try_get_mut<UiFlex>();
    if (flex == nullptr || flex->position == UiPosition::Static) {
        return;
    }
    UiReveal& rev = s.ent[panel].ensure<UiReveal>();
    if (open) {
        const float measured = flex->height > 56.0f ? flex->height : s.box[panel].h;
        if (measured > 56.0f) {
            rev.full = measured;
        }
        rev.t0 = -1.0f;
        rev.to = 0.0f;
        flex->heightMode = UiSize::Px;
        flex->height = 52.0f;
    } else if (rev.full > 56.0f) {
        rev.t0 = -1.0f;
        rev.to = rev.full;
        flex->heightMode = UiSize::Px;
        flex->height = rev.full;
    }
    uiNotifyLayoutDirty(s);
}

} // namespace burnhope

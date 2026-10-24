#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Fold.hpp"

namespace burnhope {
namespace {

void tickFoldMarks(UiState& s) {
    UiFoldChain* chain = s.world.try_get_mut<UiFoldChain>();
    if (chain == nullptr) {
        return;
    }
    uint16_t id = chain->head;
    uint16_t guard = 0;
    while (id != kUiNone && id < s.count && guard < s.count) {
        ++guard;
        auto* fold = s.ent[id].try_get_mut<UiFold>();
        if (fold == nullptr) {
            id = kUiNone;
            break;
        }
        const uint16_t next = fold->next;
        const uint16_t clip = fold->clip;
        if (fold->open != 0 && clip < s.count && fold->body < s.count && s.box[fold->body].h > 8.0f) {
            const UiReveal* rev = s.ent[clip].try_get<UiReveal>();
            if (rev == nullptr || rev->t0 < 0.0f) {
                fold->full = s.box[fold->body].h;
            }
        }
        float angle = fold->open != 0 ? 90.0f : 0.0f;
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
        id = next;
    }
}

uint16_t makeBody(UiState& s, uint16_t parent, uint16_t head) {
    if (parent >= s.count || head >= s.count) {
        return kUiNone;
    }
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
    if (child == kUiNone) {
        return kUiNone;
    }
    UiFoldChain& chain = s.world.ensure<UiFoldChain>();
    s.ent[head].set<UiFold>({
        .body = child,
        .clip = clip,
        .next = chain.head,
    });
    chain.head = head;
    uiBind(s, head, [](void* user, uint16_t id) {
        uiFoldToggle(*static_cast<UiState*>(user), id);
    }, &s);
    uiBeforeLayoutAdd(s, tickFoldMarks);
    return child;
}

} // namespace

void uiFoldToggle(UiState& s, uint16_t head) {
    if (head >= s.count) {
        return;
    }
    auto* fold = s.ent[head].try_get_mut<UiFold>();
    if (fold == nullptr || fold->clip >= s.count) {
        return;
    }
    const bool opening = fold->open == 0;
    fold->open = opening ? 1 : 0;
    float full = fold->full;
    if (opening && full < 8.0f && fold->body < s.count && s.box[fold->body].h > 8.0f) {
        full = s.box[fold->body].h;
        fold->full = full;
    }
    if (full < 8.0f) {
        full = 28.0f;
    }
    uiShow(s, fold->body, true);
    uiReveal(s, fold->clip, full, opening);
}

void uiFoldOpen(UiState& s, uint16_t body) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* fold = s.ent[id].try_get_mut<UiFold>();
        if (fold == nullptr || fold->body != body) {
            continue;
        }
        fold->open = 1;
        uiShow(s, body, true);
        if (fold->clip < s.count) {
            if (auto* flex = s.ent[fold->clip].try_get_mut<UiFlex>()) {
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

uint16_t uiFoldAttach(UiState& s, uint16_t parent, uint16_t head) {
    const uint16_t body = makeBody(s, parent, head);
    if (body == kUiNone || head >= s.count) {
        return body;
    }
    if (auto* fold = s.ent[head].try_get_mut<UiFold>()) {
        fold->open = 1;
        if (fold->clip < s.count) {
            if (auto* flex = s.ent[fold->clip].try_get_mut<UiFlex>()) {
                flex->heightMode = UiSize::Auto;
                flex->height = 0.0f;
                flex->overflow = UiOverflow::Hidden;
            }
        }
    }
    uiShow(s, body, true);
    if (auto* paint = s.ent[head].try_get_mut<UiPaint>()) {
        paint->angle = 90.0f;
    }
    return body;
}

uint16_t uiFoldBody(UiState& s, uint16_t parent, const char* title) {
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
    uiText(s, head, title != nullptr ? title : "");
    uiTextStyle(s, head, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    uiHover(s, head);
    s.ent[head].add<UiFocusable>();
    const uint16_t body = makeBody(s, parent, head);
    uiFoldOpen(s, body);
    return body;
}

} // namespace burnhope

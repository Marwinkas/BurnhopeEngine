#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/State.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/Template.hpp"

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
    const UiTemplateId clipTpl = uiTemplateFind(s, "core/fold_clip");
    const uint16_t clip = uiSpawnTemplate(s, parent, clipTpl);
    const uint16_t child = uiTemplateSlot(s, clipTpl, clip, "body");
    if (clip == kUiNone || child == kUiNone) {
        return kUiNone;
    }
    // The body is revealed by animating the clip's height from 0, and both span the fold's parent.
    uiSetWidthPercent(s, clip, 100.0f);
    uiSetWidthPercent(s, child, 100.0f);
    if (auto* flex = s.ent[clip].try_get_mut<UiFlex>()) {
        flex->heightMode = UiSize::Px;
        flex->height = 0.0f;
        flex->minH = 0.0f;
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
        full = uiStyle(s, "fold_header", UiStyleKey::Height);
    }
    uiShow(s, fold->body, true);
    uiReveal(s, fold->clip, full, opening);
}

void uiFoldOpen(UiState& s, uint16_t body) {
    uint16_t folds[kUiCap];
    const uint16_t count = uiCollect<UiFold>(s, folds, kUiCap);
    for (uint16_t f = 0; f < count; ++f) {
        const uint16_t id = folds[f];
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
    const uint16_t head = uiSpawnTemplate(s, parent, "core/fold_header");
    if (head == kUiNone) {
        return kUiNone;
    }
    // A header runs edge to edge: it counters the padding of the container it sits in.
    if (auto* row = s.ent[head].try_get_mut<UiFlex>()) {
        row->position = UiPosition::Relative;
        row->widthMode = UiSize::Percent;
        row->width = 100.0f;
        if (parent < s.count) {
            if (const UiFlex* home = s.ent[parent].try_get<UiFlex>()) {
                const float left = home->pad > 0.0f ? home->pad : home->padL;
                const float right = home->pad > 0.0f ? home->pad : home->padR;
                if (left > 0.0f) {
                    row->marginL = -left;
                }
                if (right > 0.0f) {
                    row->marginR = -right;
                }
            }
        }
    }
    uiSetIcon(s, head, UiIcon::ChevronRight);
    uiText(s, head, title != nullptr ? title : "");
    const uint16_t body = makeBody(s, parent, head);
    uiFoldOpen(s, body);
    return body;
}

} // namespace burnhope

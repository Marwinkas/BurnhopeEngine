#include "ui/Hover.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/TreeView.hpp"

#include <yoga/Yoga.h>

#include <cmath>
#include <cstring>

namespace burnhope {
namespace {

void onTree(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* tree = s->ent[id].try_get_mut<UiTree>();
    if (tree == nullptr || tree->body == kUiNone) {
        return;
    }
    tree->open = tree->open == 0 ? 1 : 0;
    uiShow(*s, tree->body, tree->open != 0);
    for (uint16_t child = 0; child < s->count; ++child) {
        if (s->parentOf[child] != id) {
            continue;
        }
        uiText(*s, child, "");
        if (auto* paint = s->ent[child].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
            paint->icon = tree->open != 0 ? 11 : 10;
            paint->r = s->look.text[0];
            paint->g = s->look.text[1];
            paint->b = s->look.text[2];
        }
        break;
    }
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
    row.gap = 8.0f;
    row.padL = 8.0f;
    row.padR = 8.0f;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
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
    const uint16_t head = uiPanel(s, parent, row, bar);
    UiFlex chev{};
    chev.widthMode = UiSize::Px;
    chev.width = 16.0f;
    chev.heightMode = UiSize::Px;
    chev.height = 16.0f;
    chev.shrink = 0.0f;
    UiPaint markPaint{};
    markPaint.a = 0.0f;
    markPaint.borderW = 0.0f;
    markPaint.shadow = 0.0f;
    markPaint.icon = 10;
    markPaint.r = s.look.text[0];
    markPaint.g = s.look.text[1];
    markPaint.b = s.look.text[2];
    const uint16_t mark = uiPanel(s, head, chev, markPaint);
    if (mark < s.count) {
        s.ent[mark].add<UiNoHit>();
    }
    UiFlex caption{};
    caption.grow = 1.0f;
    caption.shrink = 1.0f;
    caption.heightMode = UiSize::Px;
    caption.height = 22.0f;
    caption.overflow = UiOverflow::Hidden;
    const uint16_t label = uiPanel(s, head, caption, paintLabel(s.look));
    uiText(s, label, text != nullptr ? text : "");
    uiTextStyle(s, label, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (label < s.count) {
        s.ent[label].add<UiNoHit>();
    }
    uiHover(s, head);
    s.ent[head].add<UiFocusable>();
    UiFlex body{};
    body.direction = UiFlexDir::Column;
    body.shrink = 0.0f;
    body.widthMode = UiSize::Percent;
    body.width = 100.0f;
    const uint16_t child = uiPanel(s, parent, body, paintClear());
    uiShow(s, child, false);
    s.ent[head].set<UiTree>({child, 0});
    uiBind(s, head, onTree, &s);
    return child;
}

namespace {

void propHair(UiState& s, uint16_t column) {
    if (column >= s.count || uiNodeChildCount(s, column) == 0) {
        return;
    }
    UiFlex line{};
    line.heightMode = UiSize::Px;
    line.height = 2.0f;
    line.shrink = 0.0f;
    UiPaint paint = paintToken(s.look, UiToken::Muted, 0.0f);
    paint.a = 0.45f;
    (void)uiPanel(s, column, line, paint);
}

} // namespace

void propPlace(UiState& s, uint16_t id, void*) {
    auto* grid = id < s.count ? s.ent[id].try_get_mut<UiPropGrid>() : nullptr;
    if (grid == nullptr || grid->grip >= s.count) {
        return;
    }
    for (uint16_t n = 0; n < s.count; ++n) {
        if (!s.ent[n].is_alive() || !s.ent[n].has<UiPropLabel>()) {
            continue;
        }
        bool under = false;
        for (uint16_t p = n; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (p == id) {
                under = true;
                break;
            }
        }
        if (!under) {
            continue;
        }
        const UiFlex* lab = s.ent[n].try_get<UiFlex>();
        if (lab != nullptr && lab->widthMode == UiSize::Px && lab->width > 8.0f) {
            grid->labelW = lab->width;
        }
        break;
    }
    auto* flex = s.ent[grid->grip].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    const float h = s.box[id].h > 4.0f ? s.box[id].h : flex->height;
    flex->position = UiPosition::Absolute;
    flex->posX = grid->labelW;
    flex->posY = 0.0f;
    flex->widthMode = UiSize::Px;
    flex->width = 6.0f;
    flex->heightMode = UiSize::Px;
    flex->height = h;
    flex->shrink = 0.0f;
}

const UiPropGrid* propGrid(const UiState& s, uint16_t host) {
    for (uint16_t p = host; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (const UiPropGrid* grid = s.ent[p].try_get<UiPropGrid>()) {
            return grid;
        }
    }
    return nullptr;
}

uint16_t uiPropHost(UiState& s, uint16_t parent) {
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.position = UiPosition::Relative;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    const uint16_t id = uiPanel(s, parent, column, paintClear());
    if (id == kUiNone) {
        return id;
    }
    const uint16_t grip = uiSplitBar(s, id, true);
    if (grip < s.count) {
        if (auto* flex = s.ent[grip].try_get_mut<UiFlex>()) {
            flex->position = UiPosition::Absolute;
            flex->posX = 140.0f;
            flex->posY = 0.0f;
            flex->widthMode = UiSize::Px;
            flex->width = 6.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 8.0f;
            flex->shrink = 0.0f;
        }
        s.ent[grip].add<UiSplitShare>();
    }
    s.ent[id].set<UiPropGrid>({140.0f, grip, kUiNone, kUiNone});
    s.ent[id].set<UiLayoutRefresh>({propPlace, nullptr});
    return id;
}

uint16_t uiPropRow(UiState& s, uint16_t host, const char* label) {
    const UiPropGrid* grid = propGrid(s, host);
    const float labelW = grid != nullptr && grid->labelW > 8.0f ? grid->labelW : 140.0f;
    propHair(s, host);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.widthMode = UiSize::Percent;
    line.width = 100.0f;
    line.heightMode = UiSize::Px;
    line.height = 26.0f;
    line.shrink = 0.0f;
    const uint16_t row = uiPanel(s, host, line, paintClear());
    UiFlex name{};
    name.widthMode = UiSize::Px;
    name.width = labelW;
    name.heightMode = UiSize::Px;
    name.height = 26.0f;
    name.shrink = 0.0f;
    name.overflow = UiOverflow::Hidden;
    name.padR = 8.0f;
    const uint16_t caption = uiPanel(s, row, name, paintLabel(s.look));
    uiText(s, caption, label != nullptr ? label : "");
    uiTextStyle(s, caption, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
    if (caption < s.count) {
        s.ent[caption].add<UiPropLabel>();
        s.ent[caption].add<UiPass>();
    }
    UiFlex value{};
    value.grow = 1.0f;
    value.shrink = 1.0f;
    value.heightMode = UiSize::Px;
    value.height = 26.0f;
    value.padL = 8.0f;
    return uiPanel(s, row, value, paintClear());
}

uint16_t uiPropCheck(UiState& s, uint16_t host, const char* label, uint8_t kind, uint8_t group, bool on) {
    const uint16_t value = uiPropRow(s, host, label);
    const uint16_t mark = uiCheck(s, value, "", kind, group, on);
    if (mark < s.count) {
        if (auto* flex = s.ent[mark].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = 22.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 22.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
        }
        if (auto* paint = s.ent[mark].try_get_mut<UiPaint>()) {
            paint->a = 0.0f;
            paint->borderW = 0.0f;
        }
    }
    return mark;
}

uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiNumber(s, slot, value, minV, maxV, step);
    if (id < s.count) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->height = 24.0f;
        }
    }
    return id;
}

uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu) {
    const uint16_t slot = uiPropRow(s, host, label);
    return uiMenuButton(s, slot, value, menu);
}

void uiFold(UiState& s, uint16_t id) {
    const uint16_t header = s.parentOf[id];
    const uint16_t panel = header == kUiNone ? kUiNone : s.parentOf[header];
    if (panel == kUiNone) {
        return;
    }
    bool open = false;
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != panel || child == header || s.yoga[child] == nullptr) {
            continue;
        }
        if (YGNodeStyleGetDisplay(s.yoga[child]) != YGDisplayNone) {
            open = true;
        }
    }
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == panel && child != header) {
            uiShow(s, child, !open);
        }
    }
    auto* flex = s.ent[panel].try_get_mut<UiFlex>();
    if (flex != nullptr && flex->position != UiPosition::Static) {
        UiFoldState& fold = s.ent[panel].ensure<UiFoldState>();
        if (open) {
            fold.openH = flex->height > 56.0f ? flex->height : s.box[panel].h;
            flex->heightMode = UiSize::Px;
            flex->height = 52.0f;
        } else if (fold.openH > 56.0f) {
            flex->heightMode = UiSize::Px;
            flex->height = fold.openH;
        }
        s.layoutDirty = true;
    }
}

} // namespace burnhope

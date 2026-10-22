#include "ui/Hover.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Detail.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/TreeView.hpp"

#include <yoga/Yoga.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr float kFoldSec = 0.2f;

float easeOutCubic(float u) {
    if (u < 0.0f) {
        u = 0.0f;
    }
    if (u > 1.0f) {
        u = 1.0f;
    }
    const float t = 1.0f - u;
    return 1.0f - t * t * t;
}

uint16_t foldMark(const UiState& s, uint16_t head) {
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] == head) {
            return child;
        }
    }
    return kUiNone;
}

void setFoldAngle(UiState& s, uint16_t head, float degrees) {
    const uint16_t mark = foldMark(s, head);
    if (mark >= s.count) {
        return;
    }
    if (auto* paint = s.ent[mark].try_get_mut<UiPaint>()) {
        paint->icon = 10;
        paint->angle = degrees;
    }
}

void tickFolds(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree == nullptr || tree->body >= s.count) {
            continue;
        }
        const uint16_t inner = tree->body;
        const uint16_t clip = s.parentOf[inner];
        if (inner < s.count && s.box[inner].h > 8.0f) {
            tree->full = s.box[inner].h;
        }
        if (tree->t0 < 0.0f || clip >= s.count) {
            continue;
        }
        const float u = easeOutCubic((s.time - tree->t0) / kFoldSec);
        const float h = tree->from + (tree->to - tree->from) * u;
        if (auto* flex = s.ent[clip].try_get_mut<UiFlex>()) {
            flex->heightMode = UiSize::Px;
            flex->height = h > 0.0f ? h : 0.0f;
            flex->minH = 0.0f;
            flex->overflow = UiOverflow::Hidden;
        }
        const float span = tree->full > 1.0f ? tree->full : 1.0f;
        s.ent[inner].set<UiFade>({h / span});
        const float aim = tree->to > tree->from ? 90.0f : 0.0f;
        const float start = tree->to > tree->from ? 0.0f : 90.0f;
        setFoldAngle(s, id, start + (aim - start) * u);
        s.layoutDirty = true;
        s.visualDirty = true;
        if (u >= 1.0f) {
            tree->t0 = -1.0f;
            if (auto* flex = s.ent[clip].try_get_mut<UiFlex>()) {
                if (tree->to <= 0.5f) {
                    flex->heightMode = UiSize::Px;
                    flex->height = 0.0f;
                } else {
                    flex->heightMode = UiSize::Auto;
                    flex->height = 0.0f;
                }
            }
            s.ent[inner].set<UiFade>({tree->open != 0 ? 1.0f : 0.0f});
            setFoldAngle(s, id, tree->open != 0 ? 90.0f : 0.0f);
        }
    }
}

void onTree(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* tree = s->ent[id].try_get_mut<UiTree>();
    if (tree == nullptr || tree->body == kUiNone) {
        return;
    }
    const bool opening = tree->open == 0;
    tree->open = opening ? 1 : 0;
    const uint16_t clip = s->parentOf[tree->body];
    float from = clip < s->count ? s->box[clip].h : 0.0f;
    if (!opening && from < 1.0f) {
        from = tree->full;
    }
    if (opening && tree->full < 8.0f && tree->body < s->count && s->box[tree->body].h > 8.0f) {
        tree->full = s->box[tree->body].h;
    }
    tree->from = opening ? 0.0f : from;
    tree->to = opening ? tree->full : 0.0f;
    tree->t0 = s->time;
    uiShow(*s, tree->body, true);
    s->layoutDirty = true;
    s->visualDirty = true;
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
    chev.width = 28.0f;
    chev.heightMode = UiSize::Px;
    chev.height = 28.0f;
    chev.shrink = 0.0f;
    UiPaint markPaint{};
    markPaint.a = 0.0f;
    markPaint.borderW = 0.0f;
    markPaint.shadow = 0.0f;
    markPaint.icon = 10;
    markPaint.r = s.look.accent[0][0];
    markPaint.g = s.look.accent[0][1];
    markPaint.b = s.look.accent[0][2];
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
    s.ent[head].set<UiTree>({child, 0, 0, 0.0f, 0.0f, 0.0f, -1.0f});
    uiBind(s, head, onTree, &s);
    s.beforeLayout = tickFolds;
    return child;
}

namespace {

constexpr float kPropSlot = 24.0f;
constexpr float kPropRow = 32.0f;
constexpr float kPropPad = 4.0f;

UiPaint paintBlack() {
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.a = 1.0f;
    return paint;
}

constexpr float kPropField = 108.0f;

void propFitField(UiState& s, uint16_t id, bool compress) {
    if (id >= s.count) {
        return;
    }
    auto* flex = s.ent[id].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    flex->heightMode = UiSize::Px;
    flex->height = kPropSlot;
    flex->minH = 0.0f;
    flex->widthMode = UiSize::Px;
    flex->width = kPropField;
    flex->maxWMode = UiSize::Px;
    flex->maxW = kPropField;
    flex->grow = 0.0f;
    flex->shrink = compress ? 1.0f : 0.0f;
    if (auto* field = s.ent[id].try_get_mut<UiField>()) {
        field->align = 0;
    }
}

void propHair(UiState& s, uint16_t column) {
    if (column >= s.count || uiNodeChildCount(s, column) == 0) {
        return;
    }
    UiFlex line{};
    line.heightMode = UiSize::Px;
    line.height = 1.0f;
    line.shrink = 0.0f;
    (void)uiPanel(s, column, line, paintBlack());
}

} // namespace

constexpr float kPropIndent = 28.0f;

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
    float top = s.box[id].y;
    float bot = top + (s.box[id].h > 4.0f ? s.box[id].h : flex->height);
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        const UiFlex* parentFlex = s.ent[p].try_get<UiFlex>();
        if (parentFlex == nullptr || parentFlex->overflow != UiOverflow::Scroll || s.box[p].h < 4.0f) {
            continue;
        }
        if (s.box[p].y > top) {
            top = s.box[p].y;
        }
        const float viewBot = s.box[p].y + s.box[p].h;
        if (viewBot < bot) {
            bot = viewBot;
        }
        break;
    }
    for (uint16_t n = 0; n < s.count; ++n) {
        if (!s.ent[n].is_alive() || !s.ent[n].has<UiPropFilterBar>() || s.box[n].h < 1.0f) {
            continue;
        }
        const float barBot = s.box[n].y + s.box[n].h;
        if (barBot > top && s.box[n].y < bot) {
            top = barBot;
        }
    }
    if (bot < top + 1.0f) {
        bot = top + 1.0f;
    }
    flex->position = UiPosition::Absolute;
    flex->self = UiAlignSelf::Auto;
    flex->posX = grid->labelW + kPropIndent;
    flex->posY = top - s.box[id].y;
    flex->widthMode = UiSize::Px;
    flex->width = 1.0f;
    flex->heightMode = UiSize::Px;
    flex->height = bot - top;
    flex->shrink = 0.0f;
    const UiSplitter* drag = s.ent[grid->grip].try_get<UiSplitter>();
    const bool live = s.hovered == grid->grip || (drag != nullptr && drag->armed != 0);
    if (!live) {
        if (auto* paint = s.ent[grid->grip].try_get_mut<UiPaint>()) {
            paint->token = UiToken::Custom;
            paint->r = 1.0f;
            paint->g = 1.0f;
            paint->b = 1.0f;
            paint->a = 0.08f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
        }
    }
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
            flex->self = UiAlignSelf::Auto;
            flex->posX = 140.0f + kPropIndent;
            flex->posY = 0.0f;
            flex->widthMode = UiSize::Px;
            flex->width = 1.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 8.0f;
            flex->shrink = 0.0f;
        }
        if (auto* paint = s.ent[grip].try_get_mut<UiPaint>()) {
            paint->token = UiToken::Custom;
            paint->r = 1.0f;
            paint->g = 1.0f;
            paint->b = 1.0f;
            paint->a = 0.08f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
        }
        s.ent[grip].add<UiSplitShare>();
        s.ent[grip].add<UiPaintLast>();
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
    line.height = kPropRow;
    line.padT = kPropPad;
    line.padB = kPropPad;
    line.padL = kPropIndent;
    line.shrink = 0.0f;
    line.overflow = UiOverflow::Hidden;
    const uint16_t row = uiPanel(s, host, line, paintClear());
    UiFlex name{};
    name.widthMode = UiSize::Px;
    name.width = labelW;
    name.heightMode = UiSize::Px;
    name.height = kPropSlot;
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
    value.direction = UiFlexDir::Row;
    value.align = UiAlign::Center;
    value.justify = UiJustify::Start;
    value.grow = 1.0f;
    value.shrink = 1.0f;
    value.heightMode = UiSize::Px;
    value.height = kPropSlot;
    value.padL = 4.0f;
    value.overflow = UiOverflow::Hidden;
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
            flex->shrink = 0.0f;
            flex->self = UiAlignSelf::Center;
            flex->pad = 0.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
            flex->padT = 0.0f;
            flex->padB = 0.0f;
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
    propFitField(s, id, false);
    return id;
}

uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiMenuButton(s, slot, value, menu);
    uiSetHeight(s, id, kPropSlot);
    if (id < s.count) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = kPropField;
            flex->maxWMode = UiSize::Px;
            flex->maxW = kPropField;
            flex->grow = 0.0f;
            flex->shrink = 1.0f;
        }
    }
    if (menu < s.count) {
        s.ent[menu].add<UiMenuSlide>();
    }
    return id;
}

void uiFoldOpen(UiState& s, uint16_t body) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* tree = s.ent[id].try_get_mut<UiTree>();
        if (tree == nullptr || tree->body != body) {
            continue;
        }
        tree->open = 1;
        uiShow(s, body, true);
        const uint16_t clip = s.parentOf[body];
        if (clip < s.count) {
            if (auto* flex = s.ent[clip].try_get_mut<UiFlex>()) {
                flex->heightMode = UiSize::Auto;
                flex->height = 0.0f;
                flex->overflow = UiOverflow::Hidden;
            }
        }
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] != id) {
                continue;
            }
            uiText(s, child, "");
            if (auto* paint = s.ent[child].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->icon = 10;
                paint->angle = 90.0f;
                paint->r = s.look.accent[0][0];
                paint->g = s.look.accent[0][1];
                paint->b = s.look.accent[0][2];
            }
            break;
        }
        break;
    }
}

uint16_t uiFoldBody(UiState& s, uint16_t parent, const char* title) {
    const uint16_t body = uiTreeRow(s, parent, title);
    uiFoldOpen(s, body);
    return body;
}

namespace {

void writeNumber(UiState& s, uint16_t id, float value) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::snprintf(text, sizeof(text), "%.4g", static_cast<double>(value));
    uiSetFieldText(s, id, text);
}

void onVecCtl(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    auto* ctl = s->ent[id].try_get_mut<UiVecCtl>();
    if (ctl == nullptr) {
        return;
    }
    if (ctl->which == 3) {
        ctl->linked = ctl->linked == 0 ? 1 : 0;
        if (auto* paint = s->ent[id].try_get_mut<UiPaint>()) {
            paint->icon = ctl->linked != 0 ? 13 : 12;
        }
        s->visualDirty = true;
        return;
    }
    if (ctl->which == 4) {
        for (int i = 0; i < 3; ++i) {
            const uint16_t field = ctl->field[i];
            if (field >= s->count) {
                continue;
            }
            if (const UiField* live = s->ent[field].try_get<UiField>()) {
                fieldRemember(*s, field, *live);
            }
            writeNumber(*s, field, ctl->home[i]);
        }
        return;
    }
    if (ctl->which > 2) {
        return;
    }
    const uint16_t field = ctl->field[ctl->which];
    auto* range = field < s->count ? s->ent[field].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    if (const UiField* live = s->ent[field].try_get<UiField>()) {
        fieldRemember(*s, field, *live);
    }
    const float rounded = std::round(range->value * 100.0f) / 100.0f;
    writeNumber(*s, field, rounded);
}

} // namespace

UiPropVec3 uiPropVec3(UiState& s, uint16_t host, const char* label, float x, float y, float z, float minV, float maxV, float step) {
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.justify = UiJustify::Start;
    line.grow = 1.0f;
    line.shrink = 1.0f;
    line.gap = 2.0f;
    line.heightMode = UiSize::Px;
    line.height = kPropSlot;
    const uint16_t row = uiPanel(s, slot, line, paintClear());
    const float axes[3] = {x, y, z};
    const char* letters[3] = {"X", "Y", "Z"};
    const float ink[3][3] = {
        {0.90f, 0.24f, 0.20f},
        {0.28f, 0.76f, 0.30f},
        {0.26f, 0.50f, 0.95f},
    };
    UiPropVec3 out{};
    uint16_t* ids[3] = {&out.x, &out.y, &out.z};
    auto square = [&](uint8_t icon) {
        UiFlex box{};
        box.widthMode = UiSize::Px;
        box.width = kPropSlot;
        box.heightMode = UiSize::Px;
        box.height = kPropSlot;
        box.shrink = 0.0f;
        box.minW = kPropSlot;
        box.minH = kPropSlot;
        box.self = UiAlignSelf::Center;
        UiPaint paint = paintToken(s.look, UiToken::Button, s.look.radiusSm);
        paint.icon = icon;
        const uint16_t id = uiPanel(s, row, box, paint);
        if (id < s.count) {
            uiHover(s, id);
            s.ent[id].add<UiFocusable>();
            uiBind(s, id, onVecCtl, &s);
        }
        return id;
    };
    out.lock = square(12);
    if (out.lock < s.count) {
        s.ent[out.lock].set<UiVecCtl>({});
        if (auto* ctl = s.ent[out.lock].try_get_mut<UiVecCtl>()) {
            ctl->which = 3;
            ctl->home[0] = x;
            ctl->home[1] = y;
            ctl->home[2] = z;
        }
    }
    for (int i = 0; i < 3; ++i) {
        UiFlex tag{};
        tag.widthMode = UiSize::Px;
        tag.width = kPropSlot;
        tag.heightMode = UiSize::Px;
        tag.height = kPropSlot;
        tag.shrink = 0.0f;
        UiPaint axis{};
        axis.token = UiToken::Custom;
        axis.r = ink[i][0];
        axis.g = ink[i][1];
        axis.b = ink[i][2];
        axis.a = 1.0f;
        axis.hr = ink[i][0] * 1.2f > 1.0f ? 1.0f : ink[i][0] * 1.2f;
        axis.hg = ink[i][1] * 1.2f > 1.0f ? 1.0f : ink[i][1] * 1.2f;
        axis.hb = ink[i][2] * 1.2f > 1.0f ? 1.0f : ink[i][2] * 1.2f;
        const uint16_t cap = uiPanel(s, row, tag, axis);
        uiText(s, cap, letters[i]);
        uiTextStyle(s, cap, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        if (cap < s.count) {
            uiHover(s, cap);
            s.ent[cap].add<UiFocusable>();
            uiBind(s, cap, onVecCtl, &s);
        }
        UiFlex hold{};
        hold.grow = 0.0f;
        hold.shrink = 1.0f;
        hold.heightMode = UiSize::Px;
        hold.height = kPropSlot;
        const uint16_t box = uiPanel(s, row, hold, paintClear());
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step);
        uiNumberFill(s, *ids[i], false);
        propFitField(s, *ids[i], true);
    }
    out.reset = square(14);
    UiVecCtl shared{};
    shared.field[0] = out.x;
    shared.field[1] = out.y;
    shared.field[2] = out.z;
    shared.home[0] = x;
    shared.home[1] = y;
    shared.home[2] = z;
    auto stamp = [&](uint16_t id, uint8_t which) {
        if (id >= s.count) {
            return;
        }
        UiVecCtl ctl = shared;
        ctl.which = which;
        s.ent[id].set<UiVecCtl>(ctl);
    };
    for (uint16_t child = 0; child < s.count; ++child) {
        if (s.parentOf[child] != row) {
            continue;
        }
        if (s.ent[child].try_get<UiText>() == nullptr || s.ent[child].try_get<UiField>() != nullptr) {
            continue;
        }
        const UiText* text = s.ent[child].try_get<UiText>();
        if (text == nullptr || text->len != 1) {
            continue;
        }
        const uint8_t which = text->bytes[0] == 'Y' ? 1 : (text->bytes[0] == 'Z' ? 2 : 0);
        stamp(child, which);
    }
    stamp(out.lock, 3);
    stamp(out.reset, 4);
    return out;
}

UiPropVec2 uiPropVec2(UiState& s, uint16_t host, const char* label, float x, float y, float minV, float maxV, float step) {
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.justify = UiJustify::Start;
    line.grow = 1.0f;
    line.shrink = 1.0f;
    line.gap = 2.0f;
    line.heightMode = UiSize::Px;
    line.height = kPropSlot;
    const uint16_t row = uiPanel(s, slot, line, paintClear());
    const float axes[2] = {x, y};
    const char* letters[2] = {"X", "Y"};
    const float ink[2][3] = {
        {0.90f, 0.24f, 0.20f},
        {0.28f, 0.76f, 0.30f},
    };
    UiPropVec2 out{};
    uint16_t* ids[2] = {&out.x, &out.y};
    for (int i = 0; i < 2; ++i) {
        UiFlex tag{};
        tag.widthMode = UiSize::Px;
        tag.width = kPropSlot;
        tag.heightMode = UiSize::Px;
        tag.height = kPropSlot;
        tag.shrink = 0.0f;
        UiPaint axis{};
        axis.token = UiToken::Custom;
        axis.r = ink[i][0];
        axis.g = ink[i][1];
        axis.b = ink[i][2];
        axis.a = 1.0f;
        const uint16_t cap = uiPanel(s, row, tag, axis);
        uiText(s, cap, letters[i]);
        uiTextStyle(s, cap, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        if (cap < s.count) {
            s.ent[cap].add<UiPass>();
        }
        UiFlex hold{};
        hold.grow = 0.0f;
        hold.shrink = 0.0f;
        hold.heightMode = UiSize::Px;
        hold.height = kPropSlot;
        const uint16_t box = uiPanel(s, row, hold, paintClear());
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step);
        uiNumberFill(s, *ids[i], false);
        propFitField(s, *ids[i], true);
    }
    return out;
}

bool uiPropLinked(const UiState& s, uint16_t lock) {
    if (lock >= s.count) {
        return false;
    }
    const UiVecCtl* ctl = s.ent[lock].try_get<UiVecCtl>();
    return ctl != nullptr && ctl->linked != 0;
}

uint16_t uiPropSwatch(UiState& s, uint16_t host, const char* label, float r, float g, float b) {
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex flex{};
    flex.grow = 0.0f;
    flex.shrink = 0.0f;
    flex.widthMode = UiSize::Px;
    flex.width = kPropField;
    flex.heightMode = UiSize::Px;
    flex.height = kPropSlot;
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.r = r;
    paint.g = g;
    paint.b = b;
    paint.a = 1.0f;
    paint.radius = s.look.radiusSm;
    paint.borderW = 1.0f;
    const uint16_t id = uiPanel(s, slot, flex, paint);
    if (id < s.count) {
        uiHover(s, id);
        s.ent[id].add<UiFocusable>();
    }
    return id;
}

uint16_t uiPropSection(UiState& s, uint16_t parent, const char* title, uint8_t cat, uint8_t owner) {
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    const uint16_t wrap = uiPanel(s, parent, column, paintClear());
    if (wrap == kUiNone) {
        return wrap;
    }
    s.ent[wrap].set<UiPropSection>({cat, owner, 1, 0});
    return uiFoldBody(s, wrap, title);
}

const char* propSectionTitle(const UiState& s, uint16_t wrap) {
    for (uint16_t head = 0; head < s.count; ++head) {
        if (s.parentOf[head] != wrap) {
            continue;
        }
        for (uint16_t cap = 0; cap < s.count; ++cap) {
            if (s.parentOf[cap] != head) {
                continue;
            }
            const UiText* text = s.ent[cap].try_get<UiText>();
            if (text != nullptr && text->len > 0) {
                return text->bytes;
            }
        }
    }
    return "Component";
}

void uiPropRebuildFilters(UiState& s) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* bar = s.ent[id].try_get_mut<UiPropFilterBar>();
        if (bar == nullptr) {
            continue;
        }
        uint16_t live[11]{};
        uint8_t n = 0;
        for (uint16_t section = 0; section < s.count && n < 11; ++section) {
            const UiPropSection* prop = s.ent[section].try_get<UiPropSection>();
            if (prop == nullptr || prop->live == 0) {
                continue;
            }
            live[n++] = section;
        }
        const uint8_t shown = static_cast<uint8_t>(n + 1);
        bool kept = false;
        if (bar->which != 0xFF && bar->which < n && live[bar->which] == bar->section[bar->which]) {
            kept = true;
        }
        if (!kept) {
            bar->which = 0xFF;
        }
        bar->count = shown;
        for (uint8_t i = 0; i < 12; ++i) {
            const uint16_t button = bar->button[i];
            if (button >= s.count) {
                continue;
            }
            if (i > n) {
                uiShow(s, button, false);
                bar->section[i] = kUiNone;
                continue;
            }
            uiShow(s, button, true);
            if (i == n) {
                bar->section[i] = kUiNone;
                uiText(s, button, "All");
            } else {
                bar->section[i] = live[i];
                uiText(s, button, propSectionTitle(s, live[i]));
            }
        }
        break;
    }
}

void uiPropFilterApply(UiState& s) {
    uint8_t which = 0xFF;
    uint8_t count = 0;
    const uint16_t* sections = nullptr;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropFilterBar* bar = s.ent[id].try_get<UiPropFilterBar>();
        if (bar == nullptr) {
            continue;
        }
        which = bar->which;
        count = bar->count;
        sections = bar->section;
        for (uint8_t i = 0; i < count && i < 12; ++i) {
            const uint16_t button = bar->button[i];
            if (button >= s.count) {
                continue;
            }
            const bool on = which == 0xFF ? i + 1 == count : i == which;
            auto* paint = s.ent[button].try_get_mut<UiPaint>();
            if (paint == nullptr) {
                continue;
            }
            *paint = paintToken(s.look, on ? UiToken::Accent : UiToken::Button, s.look.radiusSm);
        }
        break;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            continue;
        }
        bool match = which == 0xFF;
        if (!match && sections != nullptr && which < count) {
            match = sections[which] == id;
        }
        uiShow(s, id, section->live != 0 && match);
    }
}

void onPropFilter(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    const uint16_t bar = s->parentOf[id];
    auto* filt = bar < s->count ? s->ent[bar].try_get_mut<UiPropFilterBar>() : nullptr;
    if (filt == nullptr) {
        return;
    }
    for (uint8_t i = 0; i < filt->count && i < 12; ++i) {
        if (filt->button[i] == id) {
            filt->which = filt->section[i] == kUiNone ? 0xFF : i;
        }
    }
    uiPropFilterApply(*s);
}

uint16_t uiPropFilterBar(UiState& s, uint16_t parent) {
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.wrap = UiFlexWrap::Wrap;
    row.gap = 4.0f;
    row.pad = 4.0f;
    row.shrink = 0.0f;
    row.minH = 30.0f;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    const uint16_t bar = uiPanel(s, parent, row, paintToken(s.look, UiToken::Content, 0.0f));
    if (bar == kUiNone) {
        return bar;
    }
    s.ent[bar].set<UiPropFilterBar>({});
    auto* filt = s.ent[bar].try_get_mut<UiPropFilterBar>();
    for (uint8_t i = 0; i < 12; ++i) {
        const uint16_t button = uiButton(s, bar, {.label = ""});
        uiSetHeight(s, button, 22.0f);
        uiBind(s, button, onPropFilter, &s);
        uiShow(s, button, false);
        if (filt != nullptr) {
            filt->button[i] = button;
            filt->section[i] = kUiNone;
        }
    }
    return bar;
}

void uiPropShowMask(UiState& s, uint8_t mask) {
    for (uint16_t id = 0; id < s.count; ++id) {
        auto* section = s.ent[id].try_get_mut<UiPropSection>();
        if (section == nullptr) {
            continue;
        }
        section->live = (section->owner & mask) != 0 ? 1 : 0;
    }
    uiPropRebuildFilters(s);
    uiPropFilterApply(s);
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

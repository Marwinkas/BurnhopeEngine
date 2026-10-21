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
            paint->r = s->look.accent[0][0];
            paint->g = s->look.accent[0][1];
            paint->b = s->look.accent[0][2];
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

constexpr float kPropSlot = 24.0f;
constexpr float kPropRow = 32.0f;
constexpr float kPropPad = 4.0f;

UiPaint paintBlack() {
    UiPaint paint{};
    paint.token = UiToken::Custom;
    paint.a = 1.0f;
    return paint;
}

void propFitField(UiState& s, uint16_t id) {
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
    flex->shrink = 1.0f;
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
    flex->self = UiAlignSelf::Auto;
    flex->posX = grid->labelW;
    flex->posY = 0.0f;
    flex->widthMode = UiSize::Px;
    flex->width = 2.0f;
    flex->heightMode = UiSize::Px;
    flex->height = h;
    flex->shrink = 0.0f;
    if (auto* paint = s.ent[grid->grip].try_get_mut<UiPaint>()) {
        *paint = paintBlack();
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
            flex->posX = 140.0f;
            flex->posY = 0.0f;
            flex->widthMode = UiSize::Px;
            flex->width = 2.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 8.0f;
            flex->shrink = 0.0f;
        }
        if (auto* paint = s.ent[grip].try_get_mut<UiPaint>()) {
            *paint = paintBlack();
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
    line.height = kPropRow;
    line.padT = kPropPad;
    line.padB = kPropPad;
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
    value.grow = 1.0f;
    value.shrink = 1.0f;
    value.heightMode = UiSize::Px;
    value.height = kPropSlot;
    value.padL = 8.0f;
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
    propFitField(s, id);
    return id;
}

uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiMenuButton(s, slot, value, menu);
    uiSetHeight(s, id, kPropSlot);
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
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] != id) {
                continue;
            }
            uiText(s, child, "");
            if (auto* paint = s.ent[child].try_get_mut<UiPaint>()) {
                paint->a = 0.0f;
                paint->borderW = 0.0f;
                paint->shadow = 0.0f;
                paint->icon = 11;
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

UiPropVec3 uiPropVec3(UiState& s, uint16_t host, const char* label, float x, float y, float z, float minV, float maxV, float step) {
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex line{};
    line.direction = UiFlexDir::Row;
    line.align = UiAlign::Center;
    line.grow = 1.0f;
    line.gap = 4.0f;
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
    for (int i = 0; i < 3; ++i) {
        UiFlex tag{};
        tag.widthMode = UiSize::Px;
        tag.width = 14.0f;
        tag.heightMode = UiSize::Px;
        tag.height = kPropSlot;
        tag.shrink = 0.0f;
        UiPaint axis{};
        axis.token = UiToken::Custom;
        axis.r = ink[i][0];
        axis.g = ink[i][1];
        axis.b = ink[i][2];
        const uint16_t cap = uiPanel(s, row, tag, axis);
        uiText(s, cap, letters[i]);
        uiTextStyle(s, cap, UiTextAlign::Left, UiTextEllipsis::End, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        UiFlex hold{};
        hold.grow = 1.0f;
        hold.shrink = 1.0f;
        hold.heightMode = UiSize::Px;
        hold.height = kPropSlot;
        const uint16_t box = uiPanel(s, row, hold, paintClear());
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step);
        propFitField(s, *ids[i]);
    }
    return out;
}

uint16_t uiPropSwatch(UiState& s, uint16_t host, const char* label, float r, float g, float b) {
    const uint16_t slot = uiPropRow(s, host, label);
    UiFlex flex{};
    flex.grow = 1.0f;
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

void uiPropFilterApply(UiState& s) {
    uint8_t which = static_cast<uint8_t>(UiPropCat::All);
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropFilterBar* bar = s.ent[id].try_get<UiPropFilterBar>();
        if (bar == nullptr) {
            continue;
        }
        which = bar->which;
        for (uint8_t i = 0; i < 8; ++i) {
            const uint16_t button = bar->button[i];
            if (button >= s.count) {
                continue;
            }
            auto* paint = s.ent[button].try_get_mut<UiPaint>();
            if (paint == nullptr) {
                continue;
            }
            *paint = paintToken(s.look, i == which ? UiToken::Accent : UiToken::Button, s.look.radiusSm);
        }
        break;
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            continue;
        }
        const bool match = which == static_cast<uint8_t>(UiPropCat::All) || section->cat == which;
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
    for (uint8_t i = 0; i < 8; ++i) {
        if (filt->button[i] == id) {
            filt->which = i;
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
    const char* labels[] = {"General", "Actor", "LOD", "Misc", "Physics", "Rendering", "Streaming", "All"};
    for (uint8_t i = 0; i < 8; ++i) {
        const uint16_t button = uiButton(s, bar, {.label = labels[i]});
        uiSetHeight(s, button, 22.0f);
        uiBind(s, button, onPropFilter, &s);
        if (filt != nullptr) {
            filt->button[i] = button;
        }
    }
    uiPropFilterApply(s);
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

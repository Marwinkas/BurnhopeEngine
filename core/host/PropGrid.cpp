#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Interaction.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Search.hpp"
#include "host/PropGrid.hpp"
#include "host/PropRoll.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/State.hpp"

#include <cmath>
#include <format>
#include <cstring>

namespace burnhope {
namespace {

// Idle column grip: a faint hairline in the theme's text color.
void gripIdle(UiPaint& paint, const UiLook& look) {
    paint.token = UiToken::Custom;
    paintTint(paint, look.text);
    paint.a = 0.08f;
    paint.borderW = 0.0f;
    paint.shadow = 0.0f;
}

constexpr float kPropSlot = 24.0f;
constexpr float kPropRow = 32.0f;
constexpr float kPropPad = 4.0f;
constexpr float kPropControlMax = 240.0f;
constexpr float kPropMinLabel = 70.0f;
constexpr float kPropMinValue = 110.0f;

UiPaint paintHair(const UiLook& look) {
    UiPaint paint = paintToken(look, UiToken::Item, 0.0f);
    paint.a = 0.35f;
    return paint;
}

void propHair(UiState& s, uint16_t column) {
    if (column >= s.count || uiNodeChildCount(s, column) == 0) {
        return;
    }
    UiFlex line{};
    line.heightMode = UiSize::Px;
    line.height = 1.0f;
    line.shrink = 0.0f;
    (void)uiPanel(s, column, line, paintHair(s.look));
}

} // namespace

constexpr float kPropIndent = 28.0f;
constexpr float kPropField = 108.0f;
constexpr float kPropMargin = 8.0f;
constexpr float kPropLock = 18.0f;

void propLinkLabel(UiState& s, uint16_t caption) {
    UiPropRoll& roll = s.world.ensure<UiPropRoll>();
    uint16_t next = roll.labels;
    if (next >= s.count || !s.ent[next].is_alive() || !s.ent[next].has<UiPropLabel>()) {
        next = kUiNone;
    }
    s.ent[caption].set<UiPropLabel>({next});
    roll.labels = caption;
}

bool propUnder(const UiState& s, uint16_t id, uint16_t host) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == host) {
            return true;
        }
    }
    return false;
}

uint16_t propBar(const UiState& s) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    const uint16_t bar = roll != nullptr ? roll->bar : kUiNone;
    if (bar >= s.count || !s.ent[bar].is_alive() || !s.ent[bar].has<UiPropFilterBar>()) {
        return kUiNone;
    }
    return bar;
}

// The widest label column a panel `span` px wide can hold while the value column keeps kPropMinValue.
float propLabelLimit(float span, float indent) {
    const float room = span - indent - kPropMinValue;
    return room > kPropMinLabel ? room : kPropMinLabel;
}

// Gives every label of every property grid the width `w`.
void propApplyLabelW(UiState& s, float w) {
    propEachLabel(s, [&](uint16_t id) {
        const UiFlex* flex = s.ent[id].try_get<UiFlex>();
        if (flex != nullptr && (flex->widthMode != UiSize::Px || flex->width != w)) {
            uiSetWidth(s, id, w);
        }
        return true;
    });
}

// Screen y of the first visible property label under `host`, clamped into [top, top + span]; `top` when none shows.
// The grip starts here so it never covers a tab bar, header or material preview above the rows.
float propContentTop(const UiState& s, uint16_t host, float top, float span) {
    float first = top + span;
    bool seen = false;
    propEachLabel(s, [&](uint16_t id) {
        if (!propUnder(s, id, host) || s.vis[id].hidden != 0 || s.box[id].h < 1.0f) {
            return true;
        }
        if (s.box[id].y < first) {
            first = s.box[id].y;
            seen = true;
        }
        return true;
    });
    if (!seen) {
        return top;
    }
    return first < top ? top : first;
}

float propColumnResize(void* user, uint16_t split, float size) {
    auto& s = *static_cast<UiState*>(user);
    const UiSplitter* bound = split < s.count ? s.ent[split].try_get<UiSplitter>() : nullptr;
    const uint16_t host = bound != nullptr ? bound->target : kUiNone;
    if (host >= s.count || s.ent[host].try_get<UiPropGrid>() == nullptr) {
        return size;
    }
    const uint16_t homeId = s.parentOf[split];
    const float span = homeId < s.count ? s.box[homeId].w : 0.0f;
    float clamped = size < kPropMinLabel ? kPropMinLabel : size;
    if (span > 0.0f) {
        const float limit = propLabelLimit(span, kPropIndent);
        clamped = clamped > limit ? limit : clamped;
    }
    s.world.ensure<UiPropRoll>().labelW = clamped;
    propApplyLabelW(s, clamped);
    float gripY = 0.0f;
    float gripH = 360.0f;
    if (const UiFlex* bar = s.ent[split].try_get<UiFlex>()) {
        gripY = bar->posY;
        if (bar->height > 8.0f) {
            gripH = bar->height;
        }
    }
    uiPlace(s, split, true, clamped + kPropIndent, gripY, 2.0f, gripH);
    if (auto* grid = s.ent[host].try_get_mut<UiPropGrid>()) {
        grid->labelW = clamped;
    }
    return clamped;
}

void propPlace(UiState& s, uint16_t id, void*) {
    auto* grid = id < s.count ? s.ent[id].try_get_mut<UiPropGrid>() : nullptr;
    if (grid == nullptr || grid->grip >= s.count) {
        return;
    }
    auto* flex = s.ent[grid->grip].try_get_mut<UiFlex>();
    if (flex == nullptr) {
        return;
    }
    uint16_t scroll = kUiNone;
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.ent[p].try_get<UiScroll>() != nullptr) {
            scroll = p;
            break;
        }
    }
    if (scroll != kUiNone && s.parentOf[grid->grip] != scroll) {
        uiReparent(s, grid->grip, scroll);
    }
    const bool framed = scroll < s.count && s.box[scroll].h > 8.0f && s.box[scroll].w > 8.0f;
    const float want = s.world.ensure<UiPropRoll>().labelW;
    float labelW = want;
    if (framed) {
        const float limit = propLabelLimit(s.box[scroll].w, kPropIndent);
        labelW = want > limit ? limit : want;
    }
    if (labelW != grid->labelW) {
        grid->labelW = labelW;
        propApplyLabelW(s, labelW);
    }
    float span = flex->heightMode == UiSize::Px && flex->height > 8.0f ? flex->height : 0.0f;
    if (framed) {
        span = s.box[scroll].h;
    }
    if (span < 8.0f) {
        span = 360.0f;
    }
    uiPlace(s, grid->grip, true, grid->labelW + kPropIndent, 0.0f, 2.0f, span);
    if (framed) {
        const float top = propContentTop(s, id, s.box[scroll].y, span);
        UiBox line{};
        line.x = s.box[scroll].x + grid->labelW + kPropIndent;
        line.y = top;
        line.w = 2.0f;
        line.h = s.box[scroll].y + span - top;
        s.box[grid->grip] = line;
        s.ent[grid->grip].set<UiBounds>({line.x, line.y, line.w, line.h});
    }
    const UiSplitter* drag = s.ent[grid->grip].try_get<UiSplitter>();
    const bool live = s.hovered == grid->grip || (drag != nullptr && drag->armed != 0);
    if (!live) {
        if (auto* paint = s.ent[grid->grip].try_get_mut<UiPaint>()) {
            gripIdle(*paint, s.look);
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
    const float labelW = s.world.ensure<UiPropRoll>().labelW;
    const uint16_t grip = uiSplitBar(s, id, true);
    if (grip < s.count) {
        uiPlace(s, grip, true, labelW + kPropIndent, 0.0f, 2.0f, 360.0f);
        if (auto* paint = s.ent[grip].try_get_mut<UiPaint>()) {
            gripIdle(*paint, s.look);
        }
        uiSplitterBind(s, grip, id, 0, propColumnResize, &s);
        if (auto* drag = s.ent[grip].try_get_mut<UiSplitter>()) {
            drag->live = labelW;
            drag->startSize = labelW;
        }
        s.ent[grip].add<UiPaintLast>();
        uint16_t scroll = kUiNone;
        for (uint16_t p = parent; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].try_get<UiScroll>() != nullptr) {
                scroll = p;
                break;
            }
        }
        if (scroll != kUiNone) {
            uiReparent(s, grip, scroll);
        }
    }
    s.ent[id].set<UiPropGrid>({labelW, grip, kUiNone, kUiNone});
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
    line.padR = kPropMargin;
    line.shrink = 0.0f;
    line.overflow = UiOverflow::Hidden;
    const uint16_t row = uiPanel(s, host, line, paintClear());
    if (row < s.count) {
        s.ent[row].add<UiRowHover>();
        if (auto* paint = s.ent[row].try_get_mut<UiPaint>()) {
            paint->hoverKids = 1;
            paint->hr = s.look.text[0];
            paint->hg = s.look.text[1];
            paint->hb = s.look.text[2];
            paint->ha = 0.14f;
        }
    }
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
        propLinkLabel(s, caption);
        uiSetPass(s, caption);
    }
    UiFlex value{};
    value.direction = UiFlexDir::Row;
    value.align = UiAlign::Center;
    value.justify = UiJustify::Start;
    value.grow = 0.0f;
    value.shrink = 1.0f;
    value.widthMode = UiSize::Percent;
    value.width = 100.0f;
    value.maxWMode = UiSize::Px;
    value.maxW = kPropControlMax;
    value.heightMode = UiSize::Px;
    value.height = kPropSlot;
    value.padL = 4.0f;
    value.overflow = UiOverflow::Hidden;
    const uint16_t slot = uiPanel(s, row, value, paintClear());
    UiFlex trail{};
    trail.direction = UiFlexDir::Row;
    trail.align = UiAlign::Center;
    trail.justify = UiJustify::End;
    trail.widthMode = UiSize::Px;
    trail.width = kPropSlot;
    trail.heightMode = UiSize::Px;
    trail.height = kPropSlot;
    trail.grow = 1.0f;
    trail.shrink = 0.0f;
    trail.overflow = UiOverflow::Hidden;
    const uint16_t tail = uiPanel(s, row, trail, paintClear());
    if (tail < s.count) {
        s.ent[tail].add<UiPropTrail>();
        uiSetPass(s, tail);
    }
    return slot;
}

uint16_t uiPropTrail(const UiState& s, uint16_t node) {
    uint16_t row = node;
    while (row < s.count && !s.ent[row].has<UiRowHover>()) {
        row = s.parentOf[row];
    }
    if (row >= s.count) {
        return kUiNone;
    }
    uint16_t found = kUiNone;
    propEachChild(s, row, [&](uint16_t child) {
        if (s.ent[child].has<UiPropTrail>()) {
            found = child;
            return false;
        }
        return true;
    });
    return found;
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

// A number field takes the whole value slot, so scalar and vector rows end on the same right edge.
void propFillWidth(UiState& s, uint16_t id) {
    auto* flex = id < s.count ? s.ent[id].try_get_mut<UiFlex>() : nullptr;
    if (flex == nullptr) {
        return;
    }
    flex->widthMode = UiSize::Percent;
    flex->width = 100.0f;
    flex->grow = 1.0f;
    flex->shrink = 1.0f;
    flex->minW = 0.0f;
}

uint16_t uiPropNumber(UiState& s, uint16_t host, const char* label, float value, float minV, float maxV, float step) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiNumber(s, slot, value, minV, maxV, step, nullptr, nullptr, kPropField, kPropSlot, 0.0f);
    propFillWidth(s, id);
    return id;
}

float propMenuWidth(const UiState& s, uint16_t menu, const char* value) {
    float widest = 0.0f;
    auto consider = [&](const char* text, uint8_t len) {
        if (text == nullptr || len == 0) {
            return;
        }
        const float measured = uiMeasure(s, text, len, len);
        if (measured > widest) {
            widest = measured;
        }
    };
    if (value != nullptr) {
        uint8_t len = 0;
        while (value[len] != '\0' && len < 48) {
            ++len;
        }
        consider(value, len);
    }
    if (menu < s.count) {
        propEachChild(s, menu, [&](uint16_t child) {
            const UiText* text = s.ent[child].try_get<UiText>();
            if (text != nullptr) {
                consider(text->bytes, text->len);
            }
            return true;
        });
    }
    return widest + 56.0f;
}

uint16_t uiPropMenu(UiState& s, uint16_t host, const char* label, const char* value, uint16_t menu) {
    const uint16_t slot = uiPropRow(s, host, label);
    const uint16_t id = uiMenuButton(s, slot, value, menu);
    const float width = propMenuWidth(s, menu, value);
    if (id < s.count) {
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = width;
            flex->maxWMode = UiSize::Px;
            flex->maxW = width;
            flex->grow = 0.0f;
            flex->shrink = 1.0f;
        }
    }
    if (menu < s.count) {
        s.ent[menu].add<UiMenuSlide>();
        if (auto* flex = s.ent[menu].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = width;
        }
    }
    return id;
}
namespace {

void writeNumber(UiState& s, uint16_t id, float value) {
    auto* range = id < s.count ? s.ent[id].try_get_mut<UiRange>() : nullptr;
    if (range == nullptr) {
        return;
    }
    range->value = value;
    char text[32]{};
    std::format_to_n(text, sizeof(text) - 1, "{:.4g}", value);
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
            paint->icon = static_cast<uint8_t>(ctl->linked != 0 ? UiIcon::Lock : UiIcon::LockOpen);
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

namespace {

// Shared body of the two- and three-axis rows. Three axes also get the link lock, the reset square and
// clickable axis caps (UiVecCtl); two axes keep inert caps.
UiPropVec3 propVecN(UiState& s, uint16_t host, const char* label, int n, const float* axes, float minV, float maxV, float step) {
    const bool full = n == 3;
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
    const char* letters[3] = {"X", "Y", "Z"};
    UiPropVec3 out{};
    out.row = row;
    uint16_t* ids[3] = {&out.x, &out.y, &out.z};
    auto square = [&](uint8_t icon, uint16_t home, float side) {
        UiFlex box{};
        box.widthMode = UiSize::Px;
        box.width = side;
        box.heightMode = UiSize::Px;
        box.height = side;
        box.shrink = 0.0f;
        box.minW = side;
        box.minH = side;
        box.marginR = 4.0f;
        box.self = UiAlignSelf::Center;
        UiPaint paint{};
        paint.token = UiToken::Custom;
        paint.r = s.look.text[0];
        paint.g = s.look.text[1];
        paint.b = s.look.text[2];
        paint.a = 0.0f;
        paint.borderW = 0.0f;
        paint.shadow = 0.0f;
        paint.radius = 0.0f;
        paint.icon = icon;
        box.pad = 0.0f;
        box.padL = 0.0f;
        box.padR = 0.0f;
        box.padT = 0.0f;
        box.padB = 0.0f;
        const uint16_t id = uiPanel(s, home, box, paint);
        if (id < s.count) {
            uiHover(s, id);
            uiSetFocusable(s, id);
            uiBind(s, id, onVecCtl, &s);
        }
        return id;
    };
    if (full) {
        out.lock = square(static_cast<uint8_t>(UiIcon::LockOpen), row, kPropLock);
        if (out.lock < s.count) {
            s.ent[out.lock].set<UiVecCtl>({});
            if (auto* ctl = s.ent[out.lock].try_get_mut<UiVecCtl>()) {
                ctl->which = 3;
                ctl->home[0] = axes[0];
                ctl->home[1] = axes[1];
                ctl->home[2] = axes[2];
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        const float* ink = kUiAxis[i];
        UiFlex tag{};
        tag.widthMode = UiSize::Px;
        tag.width = kPropSlot;
        tag.heightMode = UiSize::Px;
        tag.height = kPropSlot;
        tag.shrink = 0.0f;
        UiPaint axis{};
        axis.token = UiToken::Custom;
        axis.r = ink[0];
        axis.g = ink[1];
        axis.b = ink[2];
        axis.a = 1.0f;
        if (full) {
            axis.hr = ink[0] * 1.2f > 1.0f ? 1.0f : ink[0] * 1.2f;
            axis.hg = ink[1] * 1.2f > 1.0f ? 1.0f : ink[1] * 1.2f;
            axis.hb = ink[2] * 1.2f > 1.0f ? 1.0f : ink[2] * 1.2f;
        }
        const uint16_t cap = uiPanel(s, row, tag, axis);
        uiText(s, cap, letters[i]);
        uiTextStyle(s, cap, UiTextAlign::Center, UiTextEllipsis::None, UiTextValign::Center, false, UiTextDeco::None, 1.0f);
        if (cap < s.count) {
            if (full) {
                uiHover(s, cap);
                uiSetFocusable(s, cap);
                uiBind(s, cap, onVecCtl, &s);
            } else {
                uiSetPass(s, cap);
            }
        }
        UiFlex hold{};
        hold.grow = 1.0f;
        hold.shrink = 1.0f;
        hold.minW = 0.0f;
        hold.heightMode = UiSize::Px;
        hold.height = kPropSlot;
        const uint16_t box = uiPanel(s, row, hold, paintClear());
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step, nullptr, nullptr, kPropField, kPropSlot, 1.0f);
        propFillWidth(s, *ids[i]);
        uiNumberFill(s, *ids[i], false);
    }
    if (!full) {
        return out;
    }
    const uint16_t tail = uiPropTrail(s, row);
    out.reset = square(static_cast<uint8_t>(UiIcon::Reset), tail < s.count ? tail : row, kPropSlot);
    UiVecCtl shared{};
    shared.field[0] = out.x;
    shared.field[1] = out.y;
    shared.field[2] = out.z;
    shared.home[0] = axes[0];
    shared.home[1] = axes[1];
    shared.home[2] = axes[2];
    auto stamp = [&](uint16_t id, uint8_t which) {
        if (id >= s.count) {
            return;
        }
        UiVecCtl ctl = shared;
        ctl.which = which;
        s.ent[id].set<UiVecCtl>(ctl);
    };
    propEachChild(s, row, [&](uint16_t child) {
        const UiText* text = s.ent[child].try_get<UiText>();
        if (text == nullptr || s.ent[child].try_get<UiField>() != nullptr || text->len != 1) {
            return true;
        }
        const uint8_t which = text->bytes[0] == 'Y' ? 1 : (text->bytes[0] == 'Z' ? 2 : 0);
        stamp(child, which);
        return true;
    });
    stamp(out.lock, 3);
    stamp(out.reset, 4);
    return out;
}

} // namespace

UiPropVec3 uiPropVec3(UiState& s, uint16_t host, const char* label, float x, float y, float z, float minV, float maxV, float step) {
    const float axes[3] = {x, y, z};
    return propVecN(s, host, label, 3, axes, minV, maxV, step);
}

UiPropVec2 uiPropVec2(UiState& s, uint16_t host, const char* label, float x, float y, float minV, float maxV, float step) {
    const float axes[2] = {x, y};
    const UiPropVec3 row = propVecN(s, host, label, 2, axes, minV, maxV, step);
    return {.x = row.x, .y = row.y, .row = row.row};
}

uint16_t uiPropSlotOf(const UiState& s, uint16_t control) {
    return control < s.count ? s.parentOf[control] : kUiNone;
}

uint16_t uiPropRowOf(const UiState& s, uint16_t valueSlot) {
    return valueSlot < s.count ? s.parentOf[valueSlot] : kUiNone;
}

void uiPropRowHeight(UiState& s, uint16_t valueSlot, float rowH, float innerH) {
    const uint16_t row = uiPropRowOf(s, valueSlot);
    auto* outer = row < s.count ? s.ent[row].try_get_mut<UiFlex>() : nullptr;
    auto* inner = valueSlot < s.count ? s.ent[valueSlot].try_get_mut<UiFlex>() : nullptr;
    if (outer == nullptr || inner == nullptr) {
        return;
    }
    if (outer->heightMode == UiSize::Px && outer->height == rowH && inner->heightMode == UiSize::Px && inner->height == innerH) {
        return;
    }
    outer->heightMode = UiSize::Px;
    outer->height = rowH;
    inner->heightMode = UiSize::Px;
    inner->height = innerH;
    inner->overflow = UiOverflow::Visible;
    s.layoutDirty = true;
}

void uiPropValueMax(UiState& s, uint16_t valueSlot, float maxW) {
    auto* flex = valueSlot < s.count ? s.ent[valueSlot].try_get_mut<UiFlex>() : nullptr;
    if (flex != nullptr && flex->maxW != maxW) {
        flex->maxWMode = UiSize::Px;
        flex->maxW = maxW;
        s.layoutDirty = true;
    }
}

void uiPropRowShow(UiState& s, uint16_t valueSlot, bool on) {
    const uint16_t row = uiPropRowOf(s, valueSlot);
    if (row < s.count) {
        uiShow(s, row, on);
    }
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
        uiSetFocusable(s, id);
    }
    return id;
}

} // namespace burnhope

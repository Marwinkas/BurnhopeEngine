#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Interaction.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Check.hpp"
#include "ui/widget/Search.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {
namespace {

constexpr float kPropSlot = 24.0f;
constexpr float kPropRow = 32.0f;
constexpr float kPropPad = 4.0f;

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

bool propUnder(const UiState& s, uint16_t id, uint16_t host);

void propLinkLabel(UiState& s, uint16_t caption) {
    UiPropRoll& roll = s.world.ensure<UiPropRoll>();
    uint16_t next = roll.labels;
    if (next >= s.count || !s.ent[next].is_alive() || !s.ent[next].has<UiPropLabel>()) {
        next = kUiNone;
    }
    s.ent[caption].set<UiPropLabel>({next});
    roll.labels = caption;
}

template <typename Fn>
void propEachLabel(UiState& s, Fn&& fn) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    uint16_t id = roll != nullptr ? roll->labels : kUiNone;
    while (id != kUiNone && id < s.count && s.ent[id].is_alive()) {
        const UiPropLabel* lab = s.ent[id].try_get<UiPropLabel>();
        if (lab == nullptr) {
            break;
        }
        const uint16_t next = lab->next;
        if (!fn(id)) {
            break;
        }
        id = next;
    }
}

template <typename Fn>
void propEachChild(const UiState& s, uint16_t parent, Fn&& fn) {
    const uint32_t n = uiNodeChildCount(s, parent);
    for (uint32_t i = 0; i < n; ++i) {
        const uint16_t child = uiNodeChild(s, parent, i);
        if (child == kUiNone || child >= s.count) {
            continue;
        }
        if (!fn(child)) {
            break;
        }
    }
}

void propLinkSection(UiState& s, uint16_t wrap, uint8_t cat, uint8_t owner) {
    UiPropRoll& roll = s.world.ensure<UiPropRoll>();
    uint16_t tail = roll.sectionTail;
    if (tail >= s.count || !s.ent[tail].is_alive() || !s.ent[tail].has<UiPropSection>()) {
        tail = kUiNone;
        roll.sections = kUiNone;
    }
    s.ent[wrap].set<UiPropSection>({
        .next = kUiNone,
        .cat = cat,
        .owner = owner,
        .live = 1,
    });
    if (tail == kUiNone) {
        roll.sections = wrap;
    } else if (auto* prev = s.ent[tail].try_get_mut<UiPropSection>()) {
        prev->next = wrap;
    }
    roll.sectionTail = wrap;
}

template <typename Fn>
void propEachSection(UiState& s, Fn&& fn) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    uint16_t id = roll != nullptr ? roll->sections : kUiNone;
    while (id != kUiNone && id < s.count && s.ent[id].is_alive()) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            break;
        }
        const uint16_t next = section->next;
        if (!fn(id)) {
            break;
        }
        id = next;
    }
}

uint16_t propBar(const UiState& s) {
    const UiPropRoll* roll = s.world.try_get<UiPropRoll>();
    const uint16_t bar = roll != nullptr ? roll->bar : kUiNone;
    if (bar >= s.count || !s.ent[bar].is_alive() || !s.ent[bar].has<UiPropFilterBar>()) {
        return kUiNone;
    }
    return bar;
}

float propColumnResize(void* user, uint16_t split, float size) {
    auto& s = *static_cast<UiState*>(user);
    const UiSplitter* bound = split < s.count ? s.ent[split].try_get<UiSplitter>() : nullptr;
    const uint16_t host = bound != nullptr ? bound->target : kUiNone;
    if (host >= s.count || s.ent[host].try_get<UiPropGrid>() == nullptr) {
        return size;
    }
    float indent = kPropIndent;
    propEachLabel(s, [&](uint16_t id) {
        if (!propUnder(s, id, host)) {
            return true;
        }
        const uint16_t row = s.parentOf[id];
        if (row < s.count) {
            if (const UiFlex* flex = s.ent[row].try_get<UiFlex>()) {
                if (flex->padL > 1.0f) {
                    indent = flex->padL;
                }
            }
        }
        return false;
    });
    const uint16_t homeId = s.parentOf[split];
    const float span = homeId < s.count ? s.box[homeId].w : 0.0f;
    const float maxSize = span - indent - 72.0f;
    float clamped = size;
    if (clamped < 72.0f) {
        clamped = 72.0f;
    }
    if (maxSize > 72.0f && clamped > maxSize) {
        clamped = maxSize;
    }
    propEachLabel(s, [&](uint16_t id) {
        if (propUnder(s, id, host)) {
            uiSetWidth(s, id, clamped);
        }
        return true;
    });
    float gripY = 0.0f;
    float gripH = 360.0f;
    if (const UiFlex* bar = s.ent[split].try_get<UiFlex>()) {
        gripY = bar->posY;
        if (bar->height > 8.0f) {
            gripH = bar->height;
        }
    }
    uiPlace(s, split, true, clamped + indent, gripY, 2.0f, gripH);
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
    propEachLabel(s, [&](uint16_t n) {
        if (!propUnder(s, n, id)) {
            return true;
        }
        const UiFlex* lab = s.ent[n].try_get<UiFlex>();
        if (lab != nullptr && lab->widthMode == UiSize::Px && lab->width > 8.0f) {
            grid->labelW = lab->width;
        }
        return false;
    });
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
    float posY = 0.0f;
    float span = flex->heightMode == UiSize::Px && flex->height > 8.0f ? flex->height : 0.0f;
    if (scroll < s.count && s.box[scroll].h > 8.0f) {
        float top = s.box[scroll].y;
        const float bot = top + s.box[scroll].h;
        const uint16_t insp = s.parentOf[scroll];
        const uint16_t child = propBar(s);
        if (insp < s.count && child < s.count && s.parentOf[child] == insp) {
            if (s.box[child].h >= 8.0f && s.box[child].h <= 180.0f) {
                const float edge = s.box[child].y + s.box[child].h;
                if (edge > top && edge + 8.0f < bot) {
                    top = edge;
                }
            }
        }
        posY = top - s.box[scroll].y;
        if (posY < 0.0f) {
            posY = 0.0f;
        }
        span = bot - (s.box[scroll].y + posY);
    }
    if (span < 8.0f) {
        span = 360.0f;
    }
    uiPlace(s, grid->grip, true, grid->labelW + kPropIndent, posY, 2.0f, span);
    if (scroll < s.count && s.box[scroll].h > 8.0f && s.box[scroll].w > 8.0f) {
        UiBox line{};
        line.x = s.box[scroll].x + grid->labelW + kPropIndent;
        line.y = s.box[scroll].y + posY;
        line.w = 2.0f;
        line.h = span;
        s.box[grid->grip] = line;
        s.ent[grid->grip].set<UiBounds>({line.x, line.y, line.w, line.h});
    }
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
        uiPlace(s, grip, true, 140.0f + kPropIndent, 0.0f, 2.0f, 360.0f);
        if (auto* paint = s.ent[grip].try_get_mut<UiPaint>()) {
            paint->token = UiToken::Custom;
            paint->r = 1.0f;
            paint->g = 1.0f;
            paint->b = 1.0f;
            paint->a = 0.08f;
            paint->borderW = 0.0f;
            paint->shadow = 0.0f;
        }
        uiSplitterBind(s, grip, id, 0, propColumnResize, &s);
        if (auto* drag = s.ent[grip].try_get_mut<UiSplitter>()) {
            drag->live = 140.0f;
            drag->startSize = 140.0f;
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
    const uint16_t id = uiNumber(s, slot, value, minV, maxV, step, nullptr, nullptr, kPropField, kPropSlot, 0.0f);
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
    uiSetHeight(s, id, kPropSlot);
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
        {s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2]},
        {s.look.accent[1][0], s.look.accent[1][1], s.look.accent[1][2]},
        {s.look.accent[2][0], s.look.accent[2][1], s.look.accent[2][2]},
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
        const uint16_t id = uiPanel(s, row, box, paint);
        if (id < s.count) {
            uiHover(s, id);
            s.ent[id].add<UiFocusable>();
            uiBind(s, id, onVecCtl, &s);
        }
        return id;
    };
    out.lock = square(static_cast<uint8_t>(UiIcon::LockOpen));
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
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step, nullptr, nullptr, kPropField, kPropSlot, 1.0f);
        uiNumberFill(s, *ids[i], false);
    }
    out.reset = square(static_cast<uint8_t>(UiIcon::Reset));
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
    propEachChild(s, row, [&](uint16_t child) {
        if (s.ent[child].try_get<UiText>() == nullptr || s.ent[child].try_get<UiField>() != nullptr) {
            return true;
        }
        const UiText* text = s.ent[child].try_get<UiText>();
        if (text == nullptr || text->len != 1) {
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
        {s.look.accent[0][0], s.look.accent[0][1], s.look.accent[0][2]},
        {s.look.accent[1][0], s.look.accent[1][1], s.look.accent[1][2]},
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
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step, nullptr, nullptr, kPropField, kPropSlot, 1.0f);
        uiNumberFill(s, *ids[i], false);
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
    propLinkSection(s, wrap, cat, owner);
    const uint16_t body = uiFoldBody(s, wrap, title);
    propEachChild(s, wrap, [&](uint16_t id) {
        const UiFold* fold = s.ent[id].try_get<UiFold>();
        if (fold == nullptr || fold->body != body) {
            return true;
        }
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->padL = 0.0f;
            flex->marginL = 0.0f;
        }
        return false;
    });
    if (body < s.count) {
        if (auto* flex = s.ent[body].try_get_mut<UiFlex>()) {
            flex->pad = 0.0f;
            flex->padL = 0.0f;
            flex->padR = 0.0f;
            flex->padT = 0.0f;
            flex->padB = 0.0f;
        }
    }
    return body;
}

const char* propSectionTitle(const UiState& s, uint16_t wrap) {
    const char* title = "Component";
    propEachChild(s, wrap, [&](uint16_t head) {
        const UiText* text = s.ent[head].try_get<UiText>();
        if (text == nullptr || text->len == 0) {
            return true;
        }
        title = text->bytes;
        return false;
    });
    return title;
}

void onPropFilter(void* user, uint16_t id);

uint16_t propSpawnChip(UiState& s, uint16_t chips, uint16_t section, uint8_t all) {
    const uint16_t button = uiButton(s, chips, {.label = ""});
    if (button == kUiNone) {
        return button;
    }
    uiSetHeight(s, button, 22.0f);
    uiBind(s, button, onPropFilter, &s);
    s.ent[button].set<UiPropFilterChip>({
        .section = section,
        .all = all,
    });
    return button;
}

uint16_t propFindChip(const UiState& s, uint16_t chips, uint16_t section, uint8_t all) {
    uint16_t found = kUiNone;
    propEachChild(s, chips, [&](uint16_t child) {
        const UiPropFilterChip* chip = s.ent[child].try_get<UiPropFilterChip>();
        if (chip == nullptr) {
            return true;
        }
        if (all != 0) {
            if (chip->all == 0) {
                return true;
            }
            found = child;
            return false;
        }
        if (chip->all == 0 && chip->section == section) {
            found = child;
            return false;
        }
        return true;
    });
    return found;
}

uint16_t propReuseChip(UiState& s, uint16_t chips) {
    uint16_t found = kUiNone;
    propEachChild(s, chips, [&](uint16_t child) {
        auto* chip = s.ent[child].try_get_mut<UiPropFilterChip>();
        if (chip == nullptr || chip->all != 0 || chip->live != 0) {
            return true;
        }
        found = child;
        return false;
    });
    return found;
}

void uiPropRebuildFilters(UiState& s) {
    const uint16_t id = propBar(s);
    if (auto* bar = id < s.count ? s.ent[id].try_get_mut<UiPropFilterBar>() : nullptr) {
        const uint16_t chips = bar->chips;
        if (chips >= s.count) {
            return;
        }
        propEachChild(s, chips, [&](uint16_t child) {
            if (auto* chip = s.ent[child].try_get_mut<UiPropFilterChip>()) {
                chip->live = 0;
                uiShow(s, child, false);
            }
            return true;
        });
        bool whichLive = bar->which == kUiNone;
        propEachSection(s, [&](uint16_t section) {
            const UiPropSection* prop = s.ent[section].try_get<UiPropSection>();
            if (prop == nullptr || prop->live == 0) {
                return true;
            }
            if (section == bar->which) {
                whichLive = true;
            }
            uint16_t button = propFindChip(s, chips, section, 0);
            if (button == kUiNone) {
                button = propReuseChip(s, chips);
            }
            if (button == kUiNone) {
                button = propSpawnChip(s, chips, section, 0);
            }
            if (button == kUiNone) {
                return true;
            }
            if (auto* chip = s.ent[button].try_get_mut<UiPropFilterChip>()) {
                chip->section = section;
                chip->all = 0;
                chip->live = 1;
            }
            uiShow(s, button, true);
            uiText(s, button, propSectionTitle(s, section));
            return true;
        });
        uint16_t all = propFindChip(s, chips, kUiNone, 1);
        if (all == kUiNone) {
            all = propSpawnChip(s, chips, kUiNone, 1);
        }
        if (all != kUiNone) {
            if (auto* chip = s.ent[all].try_get_mut<UiPropFilterChip>()) {
                chip->live = 1;
                chip->all = 1;
            }
            uiShow(s, all, true);
            uiText(s, all, "All");
        }
        if (!whichLive) {
            bar->which = kUiNone;
        }
    }
}

bool propQueryHas(const char* text, uint8_t len, const char* query, uint8_t qlen) {
    if (qlen == 0) {
        return true;
    }
    if (text == nullptr || len < qlen) {
        return false;
    }
    for (uint8_t i = 0; i + qlen <= len; ++i) {
        bool same = true;
        for (uint8_t k = 0; k < qlen; ++k) {
            char a = text[i + k];
            char b = query[k];
            if (a >= 'A' && a <= 'Z') {
                a = static_cast<char>(a - 'A' + 'a');
            }
            if (b >= 'A' && b <= 'Z') {
                b = static_cast<char>(b - 'A' + 'a');
            }
            if (a != b) {
                same = false;
                break;
            }
        }
        if (same) {
            return true;
        }
    }
    return false;
}

void propQueryOf(const UiState& s, const UiPropFilterBar& bar, const char*& text, uint8_t& len) {
    text = nullptr;
    len = 0;
    if (bar.search >= s.count) {
        return;
    }
    const UiField* field = s.ent[bar.search].try_get<UiField>();
    if (field == nullptr || field->len == 0) {
        return;
    }
    text = field->bytes;
    len = field->len;
}

bool propUnder(const UiState& s, uint16_t id, uint16_t host) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == host) {
            return true;
        }
    }
    return false;
}

void uiPropFilterApply(UiState& s) {
    uint16_t which = kUiNone;
    uint16_t chips = kUiNone;
    const char* query = nullptr;
    uint8_t qlen = 0;
    bool searching = false;
    if (const uint16_t id = propBar(s); id < s.count) {
        const UiPropFilterBar* bar = s.ent[id].try_get<UiPropFilterBar>();
        if (bar != nullptr) {
            which = bar->which;
            chips = bar->chips;
            propQueryOf(s, *bar, query, qlen);
            searching = qlen > 0;
            propEachChild(s, chips, [&](uint16_t child) {
                const UiPropFilterChip* chip = s.ent[child].try_get<UiPropFilterChip>();
                if (chip == nullptr || chip->live == 0) {
                    return true;
                }
                const bool on = chip->all != 0 ? which == kUiNone : chip->section == which;
                auto* paint = s.ent[child].try_get_mut<UiPaint>();
                if (paint == nullptr) {
                    return true;
                }
                *paint = paintToken(s.look, on ? UiToken::Accent : UiToken::Button, s.look.radiusSm);
                return true;
            });
        }
    }
    propEachLabel(s, [&](uint16_t id) {
        const uint16_t row = s.parentOf[id];
        if (row < s.count) {
            const UiText* text = s.ent[id].try_get<UiText>();
            const bool match = !searching || (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen));
            uiShow(s, row, match);
        }
        return true;
    });
    propEachSection(s, [&](uint16_t id) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            return true;
        }
        if (searching) {
            bool any = false;
            propEachLabel(s, [&](uint16_t label) {
                if (!propUnder(s, label, id)) {
                    return true;
                }
                const UiText* text = s.ent[label].try_get<UiText>();
                if (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen)) {
                    any = true;
                    return false;
                }
                return true;
            });
            uiShow(s, id, section->live != 0 && any);
            return true;
        }
        const bool match = which == kUiNone || which == id;
        uiShow(s, id, section->live != 0 && match);
        return true;
    });
}

void onPropSearch(void* user, const char*) {
    auto* s = static_cast<UiState*>(user);
    if (s != nullptr) {
        uiPropFilterApply(*s);
    }
}

void onPropFilter(void* user, uint16_t id) {
    auto* s = static_cast<UiState*>(user);
    if (s == nullptr || id >= s->count) {
        return;
    }
    uint16_t bar = s->parentOf[id];
    while (bar != kUiNone && bar < s->count && !s->ent[bar].has<UiPropFilterBar>()) {
        bar = s->parentOf[bar];
    }
    auto* filt = bar < s->count ? s->ent[bar].try_get_mut<UiPropFilterBar>() : nullptr;
    if (filt == nullptr) {
        return;
    }
    const UiPropFilterChip* chip = s->ent[id].try_get<UiPropFilterChip>();
    if (chip == nullptr) {
        return;
    }
    filt->which = chip->all != 0 ? kUiNone : chip->section;
    uiPropFilterApply(*s);
}

uint16_t uiPropFilterBar(UiState& s, uint16_t parent) {
    UiFlex column{};
    column.direction = UiFlexDir::Column;
    column.shrink = 0.0f;
    column.widthMode = UiSize::Percent;
    column.width = 100.0f;
    column.padL = 12.0f;
    column.padR = 8.0f;
    column.padT = 10.0f;
    column.padB = 8.0f;
    column.gap = 8.0f;
    const uint16_t bar = uiPanel(s, parent, column, paintToken(s.look, UiToken::Content, 0.0f));
    if (bar == kUiNone) {
        return bar;
    }
    s.ent[bar].set<UiPropFilterBar>({});
    s.world.ensure<UiPropRoll>().bar = bar;
    auto* filt = s.ent[bar].try_get_mut<UiPropFilterBar>();
    const uint16_t search = uiSearchField(s, bar, "Search", onPropSearch, &s, -1.0f);
    if (filt != nullptr) {
        filt->search = search;
    }
    UiFlex row{};
    row.direction = UiFlexDir::Row;
    row.wrap = UiFlexWrap::Wrap;
    row.gap = 4.0f;
    row.shrink = 0.0f;
    row.minH = 22.0f;
    row.widthMode = UiSize::Percent;
    row.width = 100.0f;
    const uint16_t chips = uiPanel(s, bar, row, paintClear());
    if (filt != nullptr) {
        filt->chips = chips;
        filt->which = kUiNone;
    }
    return bar;
}

void uiPropShowMask(UiState& s, uint8_t mask) {
    propEachSection(s, [&](uint16_t id) {
        auto* section = s.ent[id].try_get_mut<UiPropSection>();
        if (section != nullptr) {
            section->live = (section->owner & mask) != 0 ? 1 : 0;
        }
        return true;
    });
    uiPropRebuildFilters(s);
    uiPropFilterApply(s);
}

} // namespace burnhope

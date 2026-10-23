#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Interaction.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Fold.hpp"
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
    fieldSyncText(s, id);
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

bool propUnder(const UiState& s, uint16_t id, uint16_t host);

void propColumnResize(void* user, uint16_t split, float size) {
    auto& s = *static_cast<UiState*>(user);
    uint16_t host = kUiNone;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropGrid* grid = s.ent[id].try_get<UiPropGrid>();
        if (grid == nullptr || grid->grip != split) {
            continue;
        }
        host = id;
        break;
    }
    if (host == kUiNone) {
        return;
    }
    float indent = kPropIndent;
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiPropLabel>() || !propUnder(s, id, host)) {
            continue;
        }
        const uint16_t row = s.parentOf[id];
        if (row < s.count) {
            if (const UiFlex* flex = s.ent[row].try_get<UiFlex>()) {
                if (flex->padL > 1.0f) {
                    indent = flex->padL;
                }
            }
        }
        break;
    }
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
    for (uint16_t id = 0; id < s.count; ++id) {
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiPropLabel>() || !propUnder(s, id, host)) {
            continue;
        }
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->widthMode = UiSize::Px;
            flex->width = clamped;
            flex->shrink = 0.0f;
        }
    }
    if (auto* bar = s.ent[split].try_get_mut<UiFlex>()) {
        bar->position = UiPosition::Absolute;
        bar->posX = clamped + indent;
        bar->widthMode = UiSize::Px;
        bar->width = 2.0f;
    }
    if (auto* grid = s.ent[host].try_get_mut<UiPropGrid>()) {
        grid->labelW = clamped;
    }
    if (auto* drag = s.ent[split].try_get_mut<UiSplitter>()) {
        drag->live = clamped;
    }
}

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
        if (insp < s.count) {
            for (uint16_t child = 0; child < s.count; ++child) {
                if (s.parentOf[child] != insp || !s.ent[child].has<UiPropFilterBar>()) {
                    continue;
                }
                if (s.box[child].h < 8.0f || s.box[child].h > 180.0f) {
                    continue;
                }
                const float edge = s.box[child].y + s.box[child].h;
                if (edge > top && edge + 8.0f < bot) {
                    top = edge;
                }
                break;
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
    flex->position = UiPosition::Absolute;
    flex->self = UiAlignSelf::Auto;
    flex->posX = grid->labelW + kPropIndent;
    flex->posY = posY;
    flex->widthMode = UiSize::Px;
    flex->width = 2.0f;
    flex->heightMode = UiSize::Px;
    flex->height = span;
    flex->shrink = 0.0f;
    if (scroll < s.count && s.box[scroll].h > 8.0f && s.box[scroll].w > 8.0f) {
        UiBox line{};
        line.x = s.box[scroll].x + grid->labelW + kPropIndent;
        line.y = s.box[scroll].y + posY;
        line.w = 2.0f;
        line.h = span;
        s.box[grid->grip] = line;
        s.ent[grid->grip].set<UiBounds>({line.x, line.y, line.w, line.h});
    }
    auto* drag = s.ent[grid->grip].try_get_mut<UiSplitter>();
    const bool live = s.hovered == grid->grip || (drag != nullptr && drag->armed != 0);
    if (drag != nullptr && drag->armed == 0) {
        drag->live = grid->labelW;
        drag->startSize = grid->labelW;
    }
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
            flex->width = 2.0f;
            flex->heightMode = UiSize::Px;
            flex->height = 360.0f;
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
        for (uint16_t child = 0; child < s.count; ++child) {
            if (s.parentOf[child] != menu) {
                continue;
            }
            const UiText* text = s.ent[child].try_get<UiText>();
            if (text != nullptr) {
                consider(text->bytes, text->len);
            }
        }
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
        *ids[i] = uiNumber(s, box, axes[i], minV, maxV, step);
        uiNumberFill(s, *ids[i], false);
        propFitField(s, *ids[i], true);
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
    const uint16_t body = uiFoldBody(s, wrap, title);
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiTree* tree = s.ent[id].try_get<UiTree>();
        if (tree == nullptr || tree->body != body) {
            continue;
        }
        if (auto* flex = s.ent[id].try_get_mut<UiFlex>()) {
            flex->padL = 0.0f;
            flex->marginL = 0.0f;
        }
        break;
    }
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
    for (uint16_t head = 0; head < s.count; ++head) {
        if (s.parentOf[head] != wrap) {
            continue;
        }
        const UiText* text = s.ent[head].try_get<UiText>();
        if (text != nullptr && text->len > 0) {
            return text->bytes;
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
    uint8_t which = 0xFF;
    uint8_t count = 0;
    const uint16_t* sections = nullptr;
    const char* query = nullptr;
    uint8_t qlen = 0;
    bool searching = false;
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropFilterBar* bar = s.ent[id].try_get<UiPropFilterBar>();
        if (bar == nullptr) {
            continue;
        }
        which = bar->which;
        count = bar->count;
        sections = bar->section;
        propQueryOf(s, *bar, query, qlen);
        searching = qlen > 0;
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
        if (!s.ent[id].is_alive() || !s.ent[id].has<UiPropLabel>()) {
            continue;
        }
        const uint16_t row = s.parentOf[id];
        if (row >= s.count) {
            continue;
        }
        const UiText* text = s.ent[id].try_get<UiText>();
        const bool match = !searching || (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen));
        uiShow(s, row, match);
    }
    for (uint16_t id = 0; id < s.count; ++id) {
        const UiPropSection* section = s.ent[id].try_get<UiPropSection>();
        if (section == nullptr) {
            continue;
        }
        if (searching) {
            bool any = false;
            for (uint16_t label = 0; label < s.count; ++label) {
                if (!s.ent[label].is_alive() || !s.ent[label].has<UiPropLabel>() || !propUnder(s, label, id)) {
                    continue;
                }
                const UiText* text = s.ent[label].try_get<UiText>();
                if (text != nullptr && propQueryHas(text->bytes, text->len, query, qlen)) {
                    any = true;
                    break;
                }
            }
            uiShow(s, id, section->live != 0 && any);
            continue;
        }
        bool match = which == 0xFF;
        if (!match && sections != nullptr && which < count) {
            match = sections[which] == id;
        }
        uiShow(s, id, section->live != 0 && match);
    }
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
    for (uint8_t i = 0; i < filt->count && i < 12; ++i) {
        if (filt->button[i] == id) {
            filt->which = filt->section[i] == kUiNone ? 0xFF : i;
        }
    }
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
    for (uint8_t i = 0; i < 12; ++i) {
        const uint16_t button = uiButton(s, chips, {.label = ""});
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

} // namespace burnhope

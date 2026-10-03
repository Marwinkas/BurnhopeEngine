#include "ui/widget/Detail.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

void setNote(UiState& s, const char* msg) {
    auto write = [&](uint16_t id) {
        if (id == kUiNone) {
            return;
        }
        auto* text = s.ent[id].try_get_mut<UiText>();
        auto* flex = s.ent[id].try_get_mut<UiFlex>();
        if (text == nullptr) {
            return;
        }
        copyText(text->bytes, text->len, msg);
        if (flex != nullptr && flex->widthMode == static_cast<uint8_t>(UiSize::Px)) {
            flex->width = uiMeasure(s, text->bytes, text->len, text->len);
        }
    };
    write(s.statusLabel);
    write(s.formNote);
    s.visualDirty = true;
}

int daysInMonth(int year, int month) {
    static const int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int n = days[month];
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        n = 29;
    }
    return n;
}

int weekDay(int year, int month, int day) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y = year - (month < 3 ? 1 : 0);
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

void calFill(UiState& s) {
    static const char* names[] = {"", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (s.calM < 1) {
        s.calM = 12;
        --s.calY;
    }
    if (s.calM > 12) {
        s.calM = 1;
        ++s.calY;
    }
    if (s.calHead != kUiNone) {
        auto* text = s.ent[s.calHead].try_get_mut<UiText>();
        if (text != nullptr) {
            char buf[16]{};
            const char* name = names[s.calM];
            uint8_t n = 0;
            while (name[n] != '\0') {
                buf[n] = name[n];
                ++n;
            }
            buf[n++] = ' ';
            int year = s.calY;
            char digs[8]{};
            int d = 0;
            int v = year;
            do {
                digs[d++] = static_cast<char>('0' + (v % 10));
                v /= 10;
            } while (v > 0 && d < 8);
            while (d > 0) {
                buf[n++] = digs[--d];
            }
            buf[n] = '\0';
            copyText(text->bytes, text->len, buf);
        }
    }
    const int dim = daysInMonth(s.calY, s.calM);
    const int lead = weekDay(s.calY, s.calM, 1);
    std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_r(&now, &today);
    const int todayY = today.tm_year + 1900;
    const int todayM = today.tm_mon + 1;
    const int todayD = today.tm_mday;
    for (int i = 0; i < 42; ++i) {
        if (s.calDay[i] == kUiNone) {
            continue;
        }
        auto* text = s.ent[s.calDay[i]].try_get_mut<UiText>();
        auto* paint = s.ent[s.calDay[i]].try_get_mut<UiPaint>();
        const int day = i - lead + 1;
        if (text == nullptr) {
            continue;
        }
        const bool empty = day < 1 || day > dim;
        if (empty) {
            text->len = 0;
            text->bytes[0] = '\0';
        } else if (day < 10) {
            text->bytes[0] = static_cast<char>('0' + day);
            text->bytes[1] = '\0';
            text->len = 1;
        } else {
            text->bytes[0] = static_cast<char>('0' + day / 10);
            text->bytes[1] = static_cast<char>('0' + day % 10);
            text->bytes[2] = '\0';
            text->len = 2;
        }
        if (paint != nullptr) {
            paint->disabled = empty ? 1 : 0;
            if (empty) {
                paint->r = 0.08f;
                paint->g = 0.08f;
                paint->b = 0.10f;
            } else if (day == s.calSel) {
                paint->r = 0.36f;
                paint->g = 0.55f;
                paint->b = 0.92f;
            } else if (s.calY == todayY && s.calM == todayM && day == todayD) {
                paint->r = 0.28f;
                paint->g = 0.40f;
                paint->b = 0.30f;
            } else {
                paint->r = 0.16f;
                paint->g = 0.18f;
                paint->b = 0.24f;
            }
        }
    }
    s.visualDirty = true;
}

void writeDate(UiState& s, int day) {
    if (s.calTarget == kUiNone || day < 1) {
        return;
    }
    auto* field = s.ent[s.calTarget].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    char buf[16]{};
    buf[0] = static_cast<char>('0' + (s.calY / 1000) % 10);
    buf[1] = static_cast<char>('0' + (s.calY / 100) % 10);
    buf[2] = static_cast<char>('0' + (s.calY / 10) % 10);
    buf[3] = static_cast<char>('0' + s.calY % 10);
    buf[4] = '-';
    buf[5] = static_cast<char>('0' + s.calM / 10);
    buf[6] = static_cast<char>('0' + s.calM % 10);
    buf[7] = '-';
    buf[8] = static_cast<char>('0' + day / 10);
    buf[9] = static_cast<char>('0' + day % 10);
    buf[10] = '\0';
    for (uint8_t n = 0; n < 10; ++n) {
        field->bytes[n] = buf[n];
    }
    field->bytes[10] = '\0';
    field->len = 10;
    field->caret = 10;
    field->anchor = 10;
    fieldMark(s, s.calTarget);
    s.calSel = day;
    uiShow(s, s.calPanel, false);
}

void calOnOpen(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    if (s.calTarget != kUiNone) {
        const UiField* field = s.ent[s.calTarget].try_get<UiField>();
        if (field != nullptr && field->len >= 10 && field->bytes[4] == '-' && field->bytes[7] == '-') {
            int year = 0;
            int month = 0;
            int day = 0;
            for (int i = 0; i < 4; ++i) {
                year = year * 10 + (field->bytes[i] - '0');
            }
            month = (field->bytes[5] - '0') * 10 + (field->bytes[6] - '0');
            day = (field->bytes[8] - '0') * 10 + (field->bytes[9] - '0');
            if (year > 1 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month)) {
                s.calY = year;
                s.calM = month;
                s.calSel = day;
            }
        }
    }
    calFill(s);
    uiShow(s, s.calPanel, true);
}

void calOnToday(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    s.calY = tm.tm_year + 1900;
    s.calM = tm.tm_mon + 1;
    calFill(s);
    writeDate(s, tm.tm_mday);
}

void calOnPrev(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    --s.calM;
    calFill(s);
}

void calOnNext(void* user, uint16_t, int16_t) {
    auto& s = *static_cast<UiState*>(user);
    ++s.calM;
    calFill(s);
}

void calOnClose(void* user, uint16_t, int16_t) {
    uiShow(*static_cast<UiState*>(user), static_cast<UiState*>(user)->calPanel, false);
}

void calOnDay(void* user, uint16_t id, int16_t) {
    calPick(*static_cast<UiState*>(user), id);
}

void calPick(UiState& s, uint16_t id) {
    const int lead = weekDay(s.calY, s.calM, 1);
    const int dim = daysInMonth(s.calY, s.calM);
    for (int i = 0; i < 42; ++i) {
        if (s.calDay[i] != id) {
            continue;
        }
        const int day = i - lead + 1;
        if (day < 1 || day > dim) {
            return;
        }
        writeDate(s, day);
        return;
    }
}

void uiBuildCalendar(UiState& s, uint16_t parent, float x, float y, float w, float h) {
    UiFlex flex{};
    flex.position = 1;
    flex.posX = x;
    flex.posY = y;
    flex.widthMode = static_cast<uint8_t>(UiSize::Px);
    flex.width = w;
    flex.heightMode = static_cast<uint8_t>(UiSize::Px);
    flex.height = h;
    flex.pad = 10.0f;
    flex.gap = 6.0f;
    flex.shrink = 0.0f;
    s.calPanel = spawn(s, parent, flex, paint(UiRole::Panel, 0.11f, 0.12f, 0.15f, 8.0f));
    UiFlex bar{};
    bar.direction = 1;
    bar.align = 1;
    bar.shrink = 0.0f;
    bar.heightMode = static_cast<uint8_t>(UiSize::Px);
    bar.height = 32.0f;
    bar.gap = 8.0f;
    const uint16_t tools = spawn(s, s.calPanel, bar, paint(UiRole::Hidden, 0, 0, 0));
    auto mini = [&](uint16_t host, const char* name, int16_t tag, UiClickFn fn) {
        UiFlex item{};
        item.heightMode = static_cast<uint8_t>(UiSize::Px);
        item.height = 28.0f;
        item.shrink = 0.0f;
        const uint16_t id = spawn(s, host, item, paint(UiRole::Button, 0.20f, 0.24f, 0.32f, 6.0f, tag));
        addText(s, id, name);
        if (auto* box = s.ent[id].try_get_mut<UiFlex>()) {
            const UiText* label = s.ent[id].try_get<UiText>();
            const uint8_t n = label != nullptr ? label->len : 1;
            box->widthMode = static_cast<uint8_t>(UiSize::Px);
            box->width = static_cast<float>(n) * s.look.advance + 24.0f;
            box->height = 28.0f;
            box->heightMode = static_cast<uint8_t>(UiSize::Px);
        }
        uiBind(s, id, fn, &s);
    };
    mini(tools, "<", -54, calOnPrev);
    s.calHead = spawn(s, tools, UiFlex{}, paint(UiRole::Label, 0.9f, 0.92f, 0.96f));
    addText(s, s.calHead, "OCT 2026");
    mini(tools, ">", -55, calOnNext);
    mini(tools, "X", -57, calOnClose);
    UiFlex todayRow{};
    todayRow.direction = 1;
    todayRow.shrink = 0.0f;
    todayRow.heightMode = static_cast<uint8_t>(UiSize::Px);
    todayRow.height = 32.0f;
    mini(spawn(s, s.calPanel, todayRow, paint(UiRole::Hidden, 0, 0, 0)), "TODAY", -87, calOnToday);
    UiFlex weekHead{};
    weekHead.direction = 1;
    weekHead.gap = 4.0f;
    weekHead.shrink = 0.0f;
    weekHead.heightMode = static_cast<uint8_t>(UiSize::Px);
    weekHead.height = 20.0f;
    const uint16_t weekHeadId = spawn(s, s.calPanel, weekHead, paint(UiRole::Hidden, 0, 0, 0));
    const char* weekNames[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int day = 0; day < 7; ++day) {
        const uint16_t id = spawn(s, weekHeadId, px(32.0f, 18.0f), paint(UiRole::Label, 0.55f, 0.58f, 0.64f));
        addText(s, id, weekNames[day]);
    }
    for (int week = 0; week < 6; ++week) {
        UiFlex weekRow{};
        weekRow.direction = 1;
        weekRow.gap = 4.0f;
        weekRow.shrink = 0.0f;
        weekRow.heightMode = static_cast<uint8_t>(UiSize::Px);
        weekRow.height = 32.0f;
        const uint16_t weekId = spawn(s, s.calPanel, weekRow, paint(UiRole::Hidden, 0, 0, 0));
        for (int day = 0; day < 7; ++day) {
            const uint16_t id = spawn(s, weekId, px(32.0f, 28.0f), paint(UiRole::Button, 0.16f, 0.18f, 0.24f, 4.0f, -56));
            uiBind(s, id, calOnDay, &s);
            s.calDay[week * 7 + day] = id;
            s.ent[id].set<UiText>(UiText{});
        }
    }
    calFill(s);
    uiShow(s, s.calPanel, false);
}

} // namespace burnhope

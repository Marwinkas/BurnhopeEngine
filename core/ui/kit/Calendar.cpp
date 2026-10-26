#include "Str.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/UiBin.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/Panel.hpp"
#include "ui/Style.hpp"
#include "ui/LayoutBridge.hpp"
#include "ui/State.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

void calPick(UiState& s, UiCalendarComponent& cal, uint16_t id);

bool calOwns(const UiCalendarComponent& cal, uint16_t id) {
    if (id == kUiNone) {
        return false;
    }
    return id == cal.calPanel || id == cal.calHead || id == cal.calTarget;
}

bool calDayOf(const UiState& s, uint16_t id, uint16_t panel) {
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == panel) {
            return true;
        }
    }
    return false;
}

UiCalendarComponent* calFor(UiState& s, uint16_t id) {
    if (id < s.count) {
        for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            if (s.ent[p].is_alive() && s.ent[p].has<UiCalendarComponent>()) {
                return s.ent[p].try_get_mut<UiCalendarComponent>();
            }
        }
        uint16_t holders[kUiCap];
        const uint16_t n = uiCollect<UiCalendarComponent>(s, holders, kUiCap);
        for (uint16_t h = 0; h < n; ++h) {
            UiCalendarComponent* cal = s.ent[holders[h]].try_get_mut<UiCalendarComponent>();
            if (cal != nullptr && calOwns(*cal, id)) {
                return cal;
            }
        }
    }
    return uiCalFind(s);
}

UiCalendarComponent& calBind(UiState& s, uint16_t panel) {
    if (panel < s.count && s.ent[panel].has<UiCalendarComponent>()) {
        return *s.ent[panel].try_get_mut<UiCalendarComponent>();
    }
    s.ent[panel].set<UiCalendarComponent>({});
    return *s.ent[panel].try_get_mut<UiCalendarComponent>();
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

void calFill(UiState& s, UiCalendarComponent& cal) {
    static const char* names[] = {"", "JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    if (cal.calM < 1) {
        cal.calM = 12;
        --cal.calY;
    }
    if (cal.calM > 12) {
        cal.calM = 1;
        ++cal.calY;
    }
    if (cal.calHead != kUiNone) {
        auto* text = s.ent[cal.calHead].try_get_mut<UiText>();
        if (text != nullptr) {
            char buf[16]{};
            const char* name = names[cal.calM];
            uint8_t n = 0;
            while (name[n] != '\0') {
                buf[n] = name[n];
                ++n;
            }
            buf[n++] = ' ';
            char yearBuf[8]{};
            uint8_t yearLen = 0;
            uiWriteInt(yearBuf, yearLen, cal.calY);
            for (uint8_t i = 0; i < yearLen; ++i) {
                buf[n++] = yearBuf[i];
            }
            buf[n] = '\0';
            copyText(text->bytes, text->len, buf);
        }
    }
    const int dim = daysInMonth(cal.calY, cal.calM);
    const int lead = weekDay(cal.calY, cal.calM, 1);
    std::time_t now = std::time(nullptr);
    std::tm today{};
    if (!platformLocalTime(&now, &today)) {
        today = {};
    }
    const int todayY = today.tm_year + 1900;
    const int todayM = today.tm_mon + 1;
    const int todayD = today.tm_mday;
    uint16_t days[kUiCap];
    const uint16_t dayCount = uiCollect<UiCalDay>(s, days, kUiCap);
    for (uint16_t d = 0; d < dayCount; ++d) {
        const uint16_t node = days[d];
        const UiCalDay* cell = s.ent[node].try_get<UiCalDay>();
        if (cell == nullptr || !calDayOf(s, node, cal.calPanel)) {
            continue;
        }
        const int i = cell->cell;
        auto* text = s.ent[node].try_get_mut<UiText>();
        auto* paint = s.ent[node].try_get_mut<UiPaint>();
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
            const bool today = cal.calY == todayY && cal.calM == todayM && day == todayD;
            if (empty) {
                paint->token = UiToken::Content;
            } else if (day == cal.calSel) {
                paint->token = UiToken::Accent;
            } else if (today) {
                paint->token = UiToken::Custom;
            } else {
                paint->token = UiToken::Item;
            }
            if (today && !empty && day != cal.calSel) {
                paint->r = s.look.accent[1][0];
                paint->g = s.look.accent[1][1];
                paint->b = s.look.accent[1][2];
            } else {
                tokenRgb(s.look, paint->token, paint->r, paint->g, paint->b);
            }
        }
    }
    s.visualDirty = true;
}

bool readIsoDate(const char* bytes, uint8_t len, int& year, int& month, int& day) {
    if (bytes == nullptr || len < 10 || bytes[4] != '-' || bytes[7] != '-') {
        return false;
    }
    year = 0;
    for (int i = 0; i < 4; ++i) {
        if (bytes[i] < '0' || bytes[i] > '9') {
            return false;
        }
        year = year * 10 + (bytes[i] - '0');
    }
    if (bytes[5] < '0' || bytes[5] > '9' || bytes[6] < '0' || bytes[6] > '9') {
        return false;
    }
    if (bytes[8] < '0' || bytes[8] > '9' || bytes[9] < '0' || bytes[9] > '9') {
        return false;
    }
    month = (bytes[5] - '0') * 10 + (bytes[6] - '0');
    day = (bytes[8] - '0') * 10 + (bytes[9] - '0');
    return year > 1 && month >= 1 && month <= 12 && day >= 1 && day <= daysInMonth(year, month);
}

void writeDate(UiState& s, UiCalendarComponent& cal, int day) {
    if (cal.calTarget == kUiNone || day < 1) {
        return;
    }
    char buf[16]{};
    buf[0] = static_cast<char>('0' + (cal.calY / 1000) % 10);
    buf[1] = static_cast<char>('0' + (cal.calY / 100) % 10);
    buf[2] = static_cast<char>('0' + (cal.calY / 10) % 10);
    buf[3] = static_cast<char>('0' + cal.calY % 10);
    buf[4] = '-';
    buf[5] = static_cast<char>('0' + cal.calM / 10);
    buf[6] = static_cast<char>('0' + cal.calM % 10);
    buf[7] = '-';
    buf[8] = static_cast<char>('0' + day / 10);
    buf[9] = static_cast<char>('0' + day % 10);
    buf[10] = '\0';
    uiSetFieldText(s, cal.calTarget, buf);
    cal.calSel = day;
    uiShow(s, cal.calPanel, false);
}

void calOnOpen(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    if ((*cal).calTarget != kUiNone && (*cal).calTarget < s.count) {
        const UiText* text = s.ent[(*cal).calTarget].try_get<UiText>();
        int year = 0;
        int month = 0;
        int day = 0;
        if (text != nullptr && readIsoDate(text->bytes, text->len, year, month, day)) {
            (*cal).calY = year;
            (*cal).calM = month;
            (*cal).calSel = day;
        }
    }
    calFill(s, *cal);
    uiShow(s, (*cal).calPanel, true);
}

void calOnToday(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    if (!platformLocalTime(&now, &tm)) {
        tm = {};
    }
    (*cal).calY = tm.tm_year + 1900;
    (*cal).calM = tm.tm_mon + 1;
    calFill(s, *cal);
    writeDate(s, *cal, tm.tm_mday);
}

void calOnPrev(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    --(*cal).calM;
    calFill(s, *cal);
}

void calOnNext(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    ++(*cal).calM;
    calFill(s, *cal);
}

void calOnClose(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);

    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    uiShow(s, (*cal).calPanel, false);
}

void calOnDay(void* user, uint16_t id) {
    auto& s = *static_cast<UiState*>(user);
    UiCalendarComponent* cal = calFor(s, id);
    if (cal == nullptr) {
        return;
    }
    calPick(s, *cal, id);
}

void calPick(UiState& s, UiCalendarComponent& cal, uint16_t id) {
    const int lead = weekDay(cal.calY, cal.calM, 1);
    const int dim = daysInMonth(cal.calY, cal.calM);
    const UiCalDay* cell = id < s.count ? s.ent[id].try_get<UiCalDay>() : nullptr;
    if (cell != nullptr) {
        const int day = static_cast<int>(cell->cell) - lead + 1;
        if (day < 1 || day > dim) {
            return;
        }
        writeDate(s, cal, day);
        return;
    }
}

uint16_t uiBuildCalendar(UiState& s, uint16_t parent, float x, float y, float w, float h) {
    UiFlex flex{};
    flex.position = UiPosition::Absolute;
    flex.posX = x;
    flex.posY = y;
    flex.widthMode = UiSize::Px;
    flex.width = w;
    flex.heightMode = UiSize::Px;
    flex.height = h;
    flex.pad = 10.0f;
    flex.gap = 6.0f;
    flex.shrink = 0.0f;
    const uint16_t panel = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Menu, 8.0f));
    if (panel == kUiNone) {
        return kUiNone;
    }
    UiCalendarComponent& cal = calBind(s, panel);
    cal.calPanel = panel;
    UiFlex bar{};
    bar.direction = UiFlexDir::Row;
    bar.align = UiAlign::Center;
    bar.shrink = 0.0f;
    bar.heightMode = UiSize::Px;
    bar.height = 32.0f;
    bar.gap = 8.0f;
    const uint16_t tools = uiPanel(s, cal.calPanel, bar, paintClear());
    auto mini = [&](uint16_t host, const char* name, UiClickFn fn) {
        const uint8_t n = name != nullptr ? static_cast<uint8_t>(strLen(name)) : 1;
        const float width = static_cast<float>(n) * s.look.advance + 24.0f;
        (void)uiButton(s, host, {.label = name, .width = width, .height = 28.0f, .onClick = fn, .user = &s});
    };
    mini(tools, "<", calOnPrev);
    cal.calHead = uiLabel(s, tools, "");
    mini(tools, ">", calOnNext);
    mini(tools, "X", calOnClose);
    UiFlex todayRow{};
    todayRow.direction = UiFlexDir::Row;
    todayRow.shrink = 0.0f;
    todayRow.heightMode = UiSize::Px;
    todayRow.height = 32.0f;
    mini(uiPanel(s, cal.calPanel, todayRow, paintClear()), "TODAY", calOnToday);
    UiFlex weekHead{};
    weekHead.direction = UiFlexDir::Row;
    weekHead.gap = 4.0f;
    weekHead.shrink = 0.0f;
    weekHead.heightMode = UiSize::Px;
    weekHead.height = 20.0f;
    const uint16_t weekHeadId = uiPanel(s, cal.calPanel, weekHead, paintClear());
    const char* weekNames[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA"};
    for (int day = 0; day < 7; ++day) {
        const uint16_t id = uiLabel(s, weekHeadId, weekNames[day]);
        if (id < s.count) {
            if (auto* box = s.ent[id].try_get_mut<UiFlex>()) {
                box->widthMode = UiSize::Px;
                box->width = 32.0f;
                box->heightMode = UiSize::Px;
                box->height = 18.0f;
            }
        }
    }
    for (int week = 0; week < 6; ++week) {
        UiFlex weekRow{};
        weekRow.direction = UiFlexDir::Row;
        weekRow.gap = 4.0f;
        weekRow.shrink = 0.0f;
        weekRow.heightMode = UiSize::Px;
        weekRow.height = 32.0f;
        const uint16_t weekId = uiPanel(s, cal.calPanel, weekRow, paintClear());
        for (int day = 0; day < 7; ++day) {
            const uint16_t id = uiButton(s, weekId, {.label = "1", .width = 32.0f, .height = 28.0f, .onClick = calOnDay, .user = &s});
            if (id < s.count) {
                s.ent[id].set<UiCalDay>({.cell = static_cast<uint8_t>(week * 7 + day)});
            }
        }
    }
    calFill(s, cal);
    uiShow(s, cal.calPanel, false);
    return panel;
}

uint16_t loadCalendarBin(UiState& s, uint16_t parent, const BinNode& node, const BinTail&, const char* name, const char*, const char*, void*) {
    const uint16_t id = uiBuildCalendar(s, parent, node.posX, node.posY, node.width, node.height);
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    return id;
}

void uiInstallCalendarBin(UiState& s) {
    uiBinRole(s, kBinSlotCalendar, "calendar", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotCalendar, loadCalendarBin, nullptr);
}

UiCalendarComponent* uiCalFind(UiState& s) {
    uint16_t holders[kUiCap];
    return uiCollect<UiCalendarComponent>(s, holders, kUiCap) > 0 ? s.ent[holders[0]].try_get_mut<UiCalendarComponent>() : nullptr;
}

const UiCalendarComponent* uiCalFind(const UiState& s) {
    uint16_t holders[kUiCap];
    return uiCollect<UiCalendarComponent>(s, holders, kUiCap) > 0 ? s.ent[holders[0]].try_get<UiCalendarComponent>() : nullptr;
}

} // namespace burnhope

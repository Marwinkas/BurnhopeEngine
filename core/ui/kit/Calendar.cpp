#include "Str.hpp"
#include "platform/PlatformUtil.hpp"
#include "ui/UiBin.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Calendar.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Label.hpp"
#include "ui/Panel.hpp"
#include "ui/LayoutBridge.hpp"

#include <cmath>
#include <cstring>
#include <ctime>

namespace burnhope {

void calPick(UiState& s, uint16_t id);

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
    if (uiCal(s).calM < 1) {
        uiCal(s).calM = 12;
        --uiCal(s).calY;
    }
    if (uiCal(s).calM > 12) {
        uiCal(s).calM = 1;
        ++uiCal(s).calY;
    }
    if (uiCal(s).calHead != kUiNone) {
        auto* text = s.ent[uiCal(s).calHead].try_get_mut<UiText>();
        if (text != nullptr) {
            char buf[16]{};
            const char* name = names[uiCal(s).calM];
            uint8_t n = 0;
            while (name[n] != '\0') {
                buf[n] = name[n];
                ++n;
            }
            buf[n++] = ' ';
            char yearBuf[8]{};
            uint8_t yearLen = 0;
            uiWriteInt(yearBuf, yearLen, uiCal(s).calY);
            for (uint8_t i = 0; i < yearLen; ++i) {
                buf[n++] = yearBuf[i];
            }
            buf[n] = '\0';
            copyText(text->bytes, text->len, buf);
        }
    }
    const int dim = daysInMonth(uiCal(s).calY, uiCal(s).calM);
    const int lead = weekDay(uiCal(s).calY, uiCal(s).calM, 1);
    std::time_t now = std::time(nullptr);
    std::tm today{};
    if (!platformLocalTime(&now, &today)) {
        today = {};
    }
    const int todayY = today.tm_year + 1900;
    const int todayM = today.tm_mon + 1;
    const int todayD = today.tm_mday;
    for (int i = 0; i < 42; ++i) {
        if (uiCal(s).calDay[i] == kUiNone) {
            continue;
        }
        auto* text = s.ent[uiCal(s).calDay[i]].try_get_mut<UiText>();
        auto* paint = s.ent[uiCal(s).calDay[i]].try_get_mut<UiPaint>();
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
            const float* rgb = s.look.item;
            if (empty) {
                rgb = s.look.content;
            } else if (day == uiCal(s).calSel) {
                rgb = s.look.accent[0];
            } else if (uiCal(s).calY == todayY && uiCal(s).calM == todayM && day == todayD) {
                rgb = s.look.accent[1];
            }
            paint->r = rgb[0];
            paint->g = rgb[1];
            paint->b = rgb[2];
        }
    }
    s.visualDirty = true;
}

void writeDate(UiState& s, int day) {
    if (uiCal(s).calTarget == kUiNone || day < 1) {
        return;
    }
    auto* field = s.ent[uiCal(s).calTarget].try_get_mut<UiField>();
    if (field == nullptr) {
        return;
    }
    char buf[16]{};
    buf[0] = static_cast<char>('0' + (uiCal(s).calY / 1000) % 10);
    buf[1] = static_cast<char>('0' + (uiCal(s).calY / 100) % 10);
    buf[2] = static_cast<char>('0' + (uiCal(s).calY / 10) % 10);
    buf[3] = static_cast<char>('0' + uiCal(s).calY % 10);
    buf[4] = '-';
    buf[5] = static_cast<char>('0' + uiCal(s).calM / 10);
    buf[6] = static_cast<char>('0' + uiCal(s).calM % 10);
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
    fieldSyncText(s, uiCal(s).calTarget);
    fieldMark(s, uiCal(s).calTarget);
    uiCal(s).calSel = day;
    uiShow(s, uiCal(s).calPanel, false);
}

void calOnOpen(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    if (uiCal(s).calTarget != kUiNone) {
        const UiField* field = s.ent[uiCal(s).calTarget].try_get<UiField>();
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
                uiCal(s).calY = year;
                uiCal(s).calM = month;
                uiCal(s).calSel = day;
            }
        }
    }
    calFill(s);
    uiShow(s, uiCal(s).calPanel, true);
}

void calOnToday(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    if (!platformLocalTime(&now, &tm)) {
        tm = {};
    }
    uiCal(s).calY = tm.tm_year + 1900;
    uiCal(s).calM = tm.tm_mon + 1;
    calFill(s);
    writeDate(s, tm.tm_mday);
}

void calOnPrev(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    --uiCal(s).calM;
    calFill(s);
}

void calOnNext(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    ++uiCal(s).calM;
    calFill(s);
}

void calOnClose(void* user, uint16_t) {
    auto& s = *static_cast<UiState*>(user);
    uiShow(s, uiCal(s).calPanel, false);
}

void calOnDay(void* user, uint16_t id) {
    calPick(*static_cast<UiState*>(user), id);
}

void calPick(UiState& s, uint16_t id) {
    const int lead = weekDay(uiCal(s).calY, uiCal(s).calM, 1);
    const int dim = daysInMonth(uiCal(s).calY, uiCal(s).calM);
    for (int i = 0; i < 42; ++i) {
        if (uiCal(s).calDay[i] != id) {
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
    uiCal(s).calPanel = uiPanel(s, parent, flex, paintToken(s.look, UiToken::Menu, 8.0f));
    UiFlex bar{};
    bar.direction = UiFlexDir::Row;
    bar.align = UiAlign::Center;
    bar.shrink = 0.0f;
    bar.heightMode = UiSize::Px;
    bar.height = 32.0f;
    bar.gap = 8.0f;
    const uint16_t tools = uiPanel(s, uiCal(s).calPanel, bar, paintClear());
    auto mini = [&](uint16_t host, const char* name, UiClickFn fn) {
        const uint8_t n = name != nullptr ? static_cast<uint8_t>(strLen(name)) : 1;
        const float width = static_cast<float>(n) * s.look.advance + 24.0f;
        (void)uiButton(s, host, {.label = name, .width = width, .height = 28.0f, .onClick = fn, .user = &s});
    };
    mini(tools, "<", calOnPrev);
    uiCal(s).calHead = uiLabel(s, tools, "OCT 2026");
    mini(tools, ">", calOnNext);
    mini(tools, "X", calOnClose);
    UiFlex todayRow{};
    todayRow.direction = UiFlexDir::Row;
    todayRow.shrink = 0.0f;
    todayRow.heightMode = UiSize::Px;
    todayRow.height = 32.0f;
    mini(uiPanel(s, uiCal(s).calPanel, todayRow, paintClear()), "TODAY", calOnToday);
    UiFlex weekHead{};
    weekHead.direction = UiFlexDir::Row;
    weekHead.gap = 4.0f;
    weekHead.shrink = 0.0f;
    weekHead.heightMode = UiSize::Px;
    weekHead.height = 20.0f;
    const uint16_t weekHeadId = uiPanel(s, uiCal(s).calPanel, weekHead, paintClear());
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
        const uint16_t weekId = uiPanel(s, uiCal(s).calPanel, weekRow, paintClear());
        for (int day = 0; day < 7; ++day) {
            const uint16_t id = uiButton(s, weekId, {.label = "1", .width = 32.0f, .height = 28.0f, .onClick = calOnDay, .user = &s});
            uiCal(s).calDay[week * 7 + day] = id;
        }
    }
    calFill(s);
    uiShow(s, uiCal(s).calPanel, false);
}

uint16_t loadCalendarBin(UiState& s, uint16_t parent, const BinNode& node, const char* name, void*) {
    uiBuildCalendar(s, parent, node.posX, node.posY, node.width, node.height);
    const uint16_t id = uiCal(s).calPanel;
    if (name != nullptr && name[0] != '\0') {
        uiName(s, id, name);
    }
    return id;
}

void uiInstallCalendarBin(UiState& s) {
    uiBinRole(s, kBinSlotCalendar, "calendar", 0, 0);
    uiRegisterCustomBinNode(s, kBinSlotCalendar, loadCalendarBin, nullptr);
}

} // namespace burnhope

#pragma once

#include "ui/Canvas.hpp"

namespace burnhope {

struct ColorBook;

// Состояние примера. Живёт у Engine и приходит в shellBuild как user.
struct ShellDemo {
    Canvas* canvas = nullptr;
    ColorBook* colors = nullptr;
    uint16_t chips = kUiNone;
    uint16_t tools = kUiNone;
    uint16_t settings = kUiNone;
    uint16_t dockHome = kUiNone;
    uint16_t dockPanel = kUiNone;
    uint8_t edges = 15;
    uint16_t scale = kUiNone;
    uint16_t preview = kUiNone;
    uint16_t scaleText = kUiNone;
    bool chipsOn = true;
    bool toolsOn = true;
    bool settingsOn = false;
    int fileDialogSlot = -1;
};

// Пример приложения. Другое приложение передаёт свой UiBuilder в hostOpen и не трогает окно, rhi и present.
void shellBuild(Canvas& canvas, void* user);
void toolBuild(Canvas& canvas, void* user);
void colorBuild(Canvas& canvas, void* user);

} // namespace burnhope

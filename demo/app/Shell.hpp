#pragma once

#include "host/Host.hpp"

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

void shellBuild(Canvas& canvas, void* user);
void toolBuild(Canvas& canvas, void* user);
void colorBuild(Canvas& canvas, void* user);
void sampleDrop(Host& host, int slot, const char* path, void* user);
void sampleOps(Host& host, int slot, const WindowOps& ops, void* user);

} // namespace burnhope

#pragma once

#include "host/Host.hpp"

namespace burnhope {

struct ColorBook;

// Ids and toggles of the sample shell built from the bin file. Callbacks receive it as their user pointer.
struct SampleShell {
    UiState* state = nullptr;
    uint16_t pages[3]{kUiNone, kUiNone, kUiNone};
    uint16_t preview = kUiNone;
    uint16_t listInner = kUiNone;
    uint16_t compactRow = kUiNone;
    uint16_t formNote = kUiNone;
    uint16_t sideId = kUiNone;
    uint16_t pageLabel = kUiNone;
    uint16_t statusLabel = kUiNone;
    int8_t page = 0;
    bool topOn = false;
    bool ghost = false;
    bool warm = false;
};

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
    SampleShell sample;
};

void uiBuildShell(UiState& s, SampleShell& shell);

void shellBuild(Canvas& canvas, void* user);
void toolBuild(Canvas& canvas, void* user);
void sampleDrop(Host& host, int slot, const char* path, void* user);
void sampleOps(Host& host, int slot, const WindowOps& ops, void* user);

} // namespace burnhope

#include "app/Shell.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "ui/rhi/Canvas.hpp"
#include "host/HostNote.hpp"
#include "platform/Window.hpp"
#include "ui/rhi/UiPhoto.hpp"
#include "Str.hpp"
#include "ui/Hover.hpp"
#include "ui/Icon.hpp"
#include "ui/Panel.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Gradient.hpp"
#include "ui/widget/Docking.hpp"
#include "ui/widget/Fold.hpp"
#include "host/PropGrid.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Number.hpp"
#include "ui/widget/Label.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/State.hpp"

#include <cstdio>
#include <cstring>

#include <spdlog/spdlog.h>

namespace burnhope {

void uiBuildShell(UiState& s);

namespace {

constexpr float kToolH = 30.0f;

void onScale(void* user, uint16_t id, float value) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || id != demo->scale) {
        return;
    }
    float minV = 0.5f;
    float maxV = 2.0f;
    float current = value;
    if (!uiRange(*demo->canvas->state, id, current, minV, maxV)) {
        return;
    }
    float t = maxV > minV ? (value - minV) / (maxV - minV) : 0.0f;
    if (t < 0.0f) {
        t = 0.0f;
    }
    if (t > 1.0f) {
        t = 1.0f;
    }
    uiSetHeight(*demo->canvas->state, demo->preview, 40.0f + t * 260.0f);
    if (demo->canvas->state != nullptr) {
        const UiLook look = canvasLook(*demo->canvas);
        uiSetPaint(*demo->canvas->state, demo->preview, look.accent[0][0], look.accent[0][1], look.accent[0][2], 0.45f + t * 0.55f);
    }
    char text[16]{};
    std::snprintf(text, sizeof(text), "SCALE %.2f", value);
    uiText(*demo->canvas->state, demo->scaleText, text);
}

void toggleShow(ShellDemo& demo, uint16_t id, bool& flag) {
    if (demo.canvas == nullptr || demo.canvas->state == nullptr) {
        return;
    }
    flag = !flag;
    uiShow(*demo.canvas->state, id, flag);
}

void toggleChips(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->chips, demo->chipsOn);
    }
}

void toggleTools(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->tools, demo->toolsOn);
    }
}

void toggleSettings(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr) {
        toggleShow(*demo, demo->settings, demo->settingsOn);
    }
}

void onDock(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiDockTo(*demo->canvas->state, 1);
    }
}

void onToolWindow(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiHostNote(*demo->canvas->state).tool = true;
    }
}

void setEdge(void* user, uint8_t bit) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || demo->canvas->state == nullptr) {
        return;
    }
    uint8_t next = static_cast<uint8_t>(demo->edges ^ bit);
    if (next == 0) {
        next = bit;
    }
    demo->edges = next;
    uiDockBind(*demo->canvas->state, demo->dockHome, demo->dockPanel, next);
}

void onEdgeLeft(void* user, uint16_t) { setEdge(user, 1); }

void onEdgeRight(void* user, uint16_t) { setEdge(user, 2); }

void onEdgeTop(void* user, uint16_t) { setEdge(user, 4); }

void onEdgeBottom(void* user, uint16_t) { setEdge(user, 8); }

void onDropLast(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 1);
    }
}

void onDropFirst(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr && demo->canvas->state != nullptr) {
        uiSetDrop(*demo->canvas->state, demo->chips, 2);
    }
}

void logPick(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr || demo->canvas == nullptr || demo->canvas->state == nullptr) {
        return;
    }
    const UiPick& pick = uiPick(*demo->canvas->state);
    char name[32]{};
    const uint8_t len = pick.len < 31 ? pick.len : 31;
    for (uint8_t i = 0; i < len; ++i) {
        name[i] = pick.name[i];
    }
    name[len] = '\0';
    spdlog::info("app pick id {} name {}", pick.id, name);
}

void ping(void*, uint16_t) {
    spdlog::info("app command: ping");
}

void growCmd(void* user, uint16_t) {
    toggleChips(user, kUiNone);
}

void warmCmd(void* user, uint16_t) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo != nullptr && demo->canvas != nullptr) {
        canvasSetLook(*demo->canvas, uiLookWarm());
    }
}

} // namespace

void onBrowse(void* user, const char* path, int isDir) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (isDir != 0 || demo == nullptr || demo->canvas == nullptr || path == nullptr || !strIsImagePath(path)) {
        return;
    }
    if (canvasLoadPhoto(*demo->canvas, 0, path)) {
        spdlog::info("browser opened image {}", path);
    }
}

void onScaleHook(void* user, float value) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (demo == nullptr) {
        return;
    }
    onScale(user, demo->scale, value);
}

void shellBuild(Canvas& canvas, void* user) {
    auto* demo = static_cast<ShellDemo*>(user);
    if (canvas.state == nullptr || demo == nullptr) {
        return;
    }
    filesLoadIcons(canvas);
    ColorBook* colors = demo->colors;
    const int fileSlot = demo->fileDialogSlot;
    *demo = {};
    demo->colors = colors;
    demo->fileDialogSlot = fileSlot;
    demo->canvas = &canvas;
    demo->chipsOn = true;
    demo->toolsOn = true;
    if (!canvasUseFont(canvas, 12.0f)) {
        spdlog::error("app font was not set");
    }
    if (canvas.state != nullptr) {
        uiBuildShell(*canvas.state);
    }
    if (colors != nullptr) {
        uiSetColorBook(*canvas.state, colors);
    }
    filesAttach(*canvas.state);
    filesMount(*canvas.state);
    filesShow(*canvas.state, true);
    demo->scale = uiFindName(*canvas.state, "scale");
    demo->preview = uiFindName(*canvas.state, "preview");
    demo->scaleText = uiFindName(*canvas.state, "scale_text");
    uiBindValue(*canvas.state, demo->scale, onScaleHook, demo);
    filesOnOpen(*canvas.state, onBrowse, demo);
    const uint16_t note = uiFindName(*canvas.state, "json_note");
    if (note != kUiNone) {
        uiText(*canvas.state, note, "named in flecs");
    }
    uiMenuStyle(*canvas.state, 210.0f, 30.0f, 8.0f, 4.0f);

    UiState& s = *canvas.state;
    const uint16_t look = uiFindName(s, "page_look");
    UiFlex block{};
    block.direction = UiFlexDir::Column;
    block.gap = 8.0f;
    block.shrink = 0.0f;
    const uint16_t section = uiPanel(s, look, block, paintClear());

    const UiLook theme = uiLook(s);
    const float textH = uiLook(s).glyphH + 6.0f;
    (void)uiButton(s, section, {.label = "YOGA GROW", .height = textH, .ghost = true, .onClick = toggleChips, .user = demo});

    UiFlex strip{};
    strip.direction = UiFlexDir::Row;
    strip.heightMode = UiSize::Px;
    strip.height = 44.0f;
    strip.gap = 8.0f;
    strip.shrink = 0.0f;
    strip.drop = UiFlexDrop::HideLast;
    const uint16_t row = uiPanel(s, section, strip, paintClear());
    demo->chips = row;
    const float grow[3] = {1.0f, 2.0f, 1.0f};
    uint16_t chipId = kUiNone;
    for (int i = 0; i < 3; ++i) {
        UiFlex chipFlex{};
        chipFlex.grow = grow[i];
        chipFlex.shrink = 1.0f;
        chipFlex.heightMode = UiSize::Percent;
        chipFlex.height = 100.0f;
        chipFlex.minW = i == 2 ? 360.0f : 0.0f;
        UiPaint chipPaint = paintRgb(theme.accent[i][0], theme.accent[i][1], theme.accent[i][2], theme.radius);
        const uint16_t piece = uiPanel(s, row, chipFlex, chipPaint);
        if (i == 0) {
            chipId = piece;
        }
        if (i == 1) {
            uiMotionTo(s, piece, theme.accent[1][0], theme.accent[1][1], theme.accent[1][2], 1.0f);
        }
        if (i == 2) {
            (void)uiLabel(s, piece, "MIN 360");
        }
    }

    UiFlex nestFlex{};
    nestFlex.direction = UiFlexDir::Column;
    nestFlex.pad = 8.0f;
    nestFlex.gap = 4.0f;
    nestFlex.shrink = 0.0f;
    const uint16_t nest = uiPanel(s, section, nestFlex, paintToken(theme, UiToken::Content, theme.radius));
    UiFlex innerFlex{};
    innerFlex.direction = UiFlexDir::Column;
    innerFlex.pad = 6.0f;
    innerFlex.shrink = 0.0f;
    const uint16_t inner = uiPanel(s, nest, innerFlex, paintToken(theme, UiToken::Item, theme.radius));
    (void)uiLabel(s, inner, "NESTED LAYER");

    (void)uiButton(s, section, {.label = "DROP LAST", .height = textH, .ghost = true, .onClick = onDropLast, .user = demo});
    (void)uiButton(s, section, {.label = "DROP FIRST", .height = textH, .ghost = true, .onClick = onDropFirst, .user = demo});
    (void)uiButton(s, section, {.label = "TOOLS", .height = textH, .ghost = true, .onClick = toggleTools, .user = demo});
    UiFlex toolsFlex{};
    toolsFlex.direction = UiFlexDir::Row;
    toolsFlex.gap = 8.0f;
    toolsFlex.shrink = 0.0f;
    toolsFlex.heightMode = UiSize::Px;
    toolsFlex.height = 36.0f;
    const uint16_t tools = uiPanel(s, section, toolsFlex, paintClear());
    demo->tools = tools;
    (void)uiButton(s, tools, {.label = "SAVE", .height = kToolH, .icon = UiIcon::Folder, .onClick = logPick, .user = demo});
    (void)uiButton(s, tools, {.label = "PICK", .height = kToolH, .icon = UiIcon::Menu, .onClick = logPick, .user = demo});
    (void)uiButton(s, tools, {.width = kToolH, .height = kToolH, .icon = UiIcon::Gear, .onClick = onToolWindow, .user = demo});
    (void)uiButton(s, tools, {.label = "WINDOW", .height = kToolH, .icon = UiIcon::Folder, .onClick = toggleSettings, .user = demo});

    UiFlex homeFlex{};
    homeFlex.direction = UiFlexDir::Column;
    homeFlex.gap = 6.0f;
    homeFlex.grow = 1.0f;
    homeFlex.shrink = 1.0f;
    homeFlex.minH = 180.0f;
    const uint16_t home = uiPanel(s, section, homeFlex, paintToken(theme, UiToken::Content, theme.radius));
    demo->dockHome = home;
    (void)uiLabel(s, home, "DOCK");

    UiFlex dockFlex{};
    dockFlex.direction = UiFlexDir::Column;
    dockFlex.gap = 6.0f;
    dockFlex.pad = 8.0f;
    dockFlex.shrink = 0.0f;
    const uint16_t dock = uiPanel(s, home, dockFlex, paintToken(theme, UiToken::Side, theme.radius));
    demo->dockPanel = dock;
    uiDockBind(s, home, dock, 15);
    const uint16_t inspector = uiDragFoldHeader(s, dock, "INSPECTOR");
    (void)uiLabel(s, inspector, "drag the header out, drop it here or press DOCK");
    (void)uiButton(s, inspector, {.label = "DOCK", .height = kToolH, .onClick = onDock, .user = demo});
    (void)uiButton(s, inspector, {.label = "LEFT", .height = textH, .ghost = true, .onClick = onEdgeLeft, .user = demo});
    (void)uiButton(s, inspector, {.label = "RIGHT", .height = textH, .ghost = true, .onClick = onEdgeRight, .user = demo});
    (void)uiButton(s, inspector, {.label = "TOP", .height = textH, .ghost = true, .onClick = onEdgeTop, .user = demo});
    (void)uiButton(s, inspector, {.label = "BOTTOM", .height = textH, .ghost = true, .onClick = onEdgeBottom, .user = demo});

    UiFlex win{};
    win.position = UiPosition::Absolute;
    win.posX = 640.0f;
    win.posY = 160.0f;
    win.widthMode = UiSize::Px;
    win.width = 300.0f;
    win.heightMode = UiSize::Px;
    win.height = 168.0f;
    win.pad = 10.0f;
    win.gap = 8.0f;
    win.shrink = 0.0f;
    const uint16_t settings = uiPanel(s, 0, win, paintToken(theme, UiToken::Window, theme.radius));
    uiSetResizable(s, settings);
    demo->settings = settings;
    const uint16_t settingsPage = uiDragFoldHeader(s, settings, "SETTINGS");
    (void)uiLabel(s, settingsPage, "separate window, drag the header");
    (void)uiButton(s, settingsPage, {.label = "CLOSE", .height = kToolH, .icon = UiIcon::Dismiss, .onClick = toggleSettings, .user = demo});
    uiShow(s, settings, false);

    const uint16_t sectionMenu = uiMakeMenu(s);
    const uint16_t chipMenu = uiMakeMenu(s);
    uiSetContextMenu(s, section, sectionMenu);
    uiSetContextMenu(s, chipId, chipMenu);
    if (!uiCommandAddTo(s, sectionMenu, "GROW", growCmd, demo)
        || !uiCommandAddTo(s, sectionMenu, "PING", ping, nullptr)
        || !uiCommandAddTo(s, chipMenu, "WARM", warmCmd, demo)) {
        spdlog::error("context menu was not filled");
    }
    if (canvas.state == nullptr || !uiCommandAdd(*canvas.state, "PING", ping, nullptr)) {
        spdlog::error("PING command was not added");
    }
}

void toolBuild(Canvas& canvas, void*) {
    if (canvas.state == nullptr || !canvasUseFont(canvas, 12.0f)) {
        spdlog::error("tool font was not set");
        if (canvas.state == nullptr) {
            return;
        }
    }
    UiState& s = *canvas.state;
    UiFlex rootFlex{};
    rootFlex.direction = UiFlexDir::Column;
    const uint16_t root = uiPanel(s, kUiNone, rootFlex, paintToken(uiLook(s), UiToken::Window, 0.0f));
    (void)uiTitleBar(s, root, "SETTINGS", nullptr, nullptr, 40.0f);
    UiFlex body{};
    body.direction = UiFlexDir::Column;
    body.pad = 16.0f;
    body.gap = 10.0f;
    body.grow = 1.0f;
    const uint16_t page = uiPanel(s, root, body, paintClear());
    (void)uiLabel(s, page, "Separate OS window.");
    (void)uiLabel(s, page, "Drag the title. Resize any edge.");
    (void)uiButton(s, page, {.label = "PING", .height = kToolH, .icon = UiIcon::Menu});
    (void)uiCurve(s, page);
    (void)uiGradient(s, page);
}

void sampleDrop(Host& host, int slot, const char* path, void*) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    Canvas* canvas = hostCanvas(host, slot);
    if (canvas == nullptr || canvas->state == nullptr) {
        return;
    }
    filesOfferPath(*canvas->state, path);
    if (strIsImagePath(path)) {
        (void)canvasLoadPhoto(*canvas, 0, path);
    } else if (strExtIs(path, ".json")) {
        (void)uiImportJsonFile(*canvas->state, 0, path);
    }
}

void sampleOps(Host& host, int slot, const WindowOps& ops, void* user) {
    (void)ops;
    auto* demo = static_cast<ShellDemo*>(user);
    ColorBook* colors = demo != nullptr ? demo->colors : nullptr;
    Canvas* canvas = hostCanvas(host, slot);
    bool wantTool = false;
    bool wantFile = false;
    if (canvas != nullptr && canvas->state != nullptr) {
        UiHostNote& note = uiHostNote(*canvas->state);
        wantTool = note.tool;
        wantFile = note.file;
        note = {};
    }
    (void)hostSyncColorWindow(host, colors);
    if (wantFile && demo != nullptr && slot >= 0 && slot < kHostSlots) {
        static const FileFilter filters[] = {
            {"All files", "*"},
            {"JSON", "json"},
            {"Images", "png;jpg;jpeg;gif;bmp;tga;hdr;psd"},
        };
        if (windowAskFile(host.view[slot].window, filters, 3, FileAsk::Open)) {
            demo->fileDialogSlot = slot;
        }
    }
    if (wantTool) {
        ViewDesc desc{};
        desc.name = "tool";
        desc.window.title = "Burnhope";
        desc.window.width = 560;
        desc.window.height = 420;
        desc.window.minWidth = 360;
        desc.window.minHeight = 240;
        desc.window.bordered = false;
        desc.window.resizable = true;
        desc.builder = toolBuild;
        desc.onOps = sampleOps;
        desc.user = demo;
        (void)hostOpen(host, desc);
    }
}

} // namespace burnhope

#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "files/FileDomain.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/Canvas.hpp"
#include "ui/Domain.hpp"
#include "ui/Panel.hpp"
#include "ui/Text.hpp"
#include "ui/UiPhoto.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/ColorPicker.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Text.hpp"
#include "ui/widget/TreeView.hpp"
#include "Str.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace burnhope {

void sceneHud(Canvas& canvas, void* user) {
    auto* hud = static_cast<HudContext*>(user);
    const bool haveFont = canvas.state != nullptr && canvas.state->font.ready;
    if (hud == nullptr || (!haveFont && !fontBesideExe(canvas) && !canvasUseFont(canvas, 20.0f)) || canvas.state == nullptr) {
        return;
    }
    uiInstallColorBin(*canvas.state);
    if (hud->book != nullptr) {
        uiSetColorBook(*canvas.state, hud->book);
    }
    hud->modes.clear();
    hud->gizmos.clear();
    hud->snaps.clear();
    hud->objects.clear();
    hud->shown = 0xff;
    hud->tool = {};
    hud->tool.canvas = &canvas;
    hud->tool.scene = hud->scene;
    hud->stats = {};
    filesLoadIcons(canvas);
    canvasSetLook(canvas, uiLookInk());
    const UiLook look = canvasLook(canvas);
    auto hiddenPaint = []() {
        UiPaint paint{};
        paint.a = 0.0f;
        return paint;
    };
    auto dockPaint = [&]() {
        UiPaint paint = paintToken(look, UiToken::Content, 0.0f);
        paint.a = 0.94f;
        paint.borderW = 0.0f;
        return paint;
    };
    UiFlex rootFlex{};
    rootFlex.grow = 1.0f;
    rootFlex.direction = UiFlexDir::Column;
    rootFlex.widthMode = UiSize::Percent;
    rootFlex.width = 100.0f;
    rootFlex.heightMode = UiSize::Percent;
    rootFlex.height = 100.0f;
    const uint16_t root = uiPanel(*canvas.state, kUiNone, rootFlex, hiddenPaint());
    UiFlex menuFlex{};
    menuFlex.direction = UiFlexDir::Row;
    menuFlex.heightMode = UiSize::Px;
    menuFlex.height = 32.0f;
    menuFlex.minH = 32.0f;
    menuFlex.shrink = 0.0f;
    menuFlex.align = UiAlign::Center;
    menuFlex.padL = 8.0f;
    menuFlex.padR = 8.0f;
    menuFlex.gap = 4.0f;
    const uint16_t menuBar = uiPanel(*canvas.state, root, menuFlex, dockPaint());
    uint16_t fileBtn = uiButton(*canvas.state, menuBar, { .label = "File" });
    uiSetHeight(*canvas.state, fileBtn, 26.0f);
    uint16_t fileMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state) {
        uiSetContextMenu(*canvas.state, fileBtn, fileMenu);
        uiMarkMenuBar(*canvas.state, fileBtn);
    }
    auto fileAct = [](void* actUser) {
        projectOnFile(static_cast<FileAct*>(actUser));
    };
    hud->fileNew = {&canvas, hud, 0};
    hud->fileOpen = {&canvas, hud, 1};
    hud->fileSave = {&canvas, hud, 2};
    hud->fileExit = {&canvas, hud, 3};
    hud->fileLoad = {&canvas, hud, 4};
    if (canvas.state != nullptr) {
        (void)uiCommandAddTo(*canvas.state, fileMenu, "New Project...", fileAct, &hud->fileNew);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Open Project...", fileAct, &hud->fileOpen);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Save Scene", fileAct, &hud->fileSave);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Load Scene", fileAct, &hud->fileLoad);
        (void)uiCommandAddTo(*canvas.state, fileMenu, "Exit", fileAct, &hud->fileExit);
    }
    uint16_t editBtn = uiButton(*canvas.state, menuBar, { .label = "Edit" });
    uiSetHeight(*canvas.state, editBtn, 26.0f);
    uint16_t editMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state != nullptr) {
        uiSetContextMenu(*canvas.state, editBtn, editMenu);
        uiMarkMenuBar(*canvas.state, editBtn);
        (void)uiCommandAddTo(*canvas.state, editMenu, "Duplicate", onDuplicateCmd, hud);
    }
    uint16_t viewMenuBtn = uiButton(*canvas.state, menuBar, { .label = "View" });
    uiSetHeight(*canvas.state, viewMenuBtn, 26.0f);
    uint16_t appViewMenu = canvas.state != nullptr ? uiMakeMenu(*canvas.state) : kUiNone;
    if (canvas.state != nullptr) {
        uiSetContextMenu(*canvas.state, viewMenuBtn, appViewMenu);
        uiMarkMenuBar(*canvas.state, viewMenuBtn);
        (void)uiCommandAddTo(*canvas.state, appViewMenu, "Stats", onStatsCmd, &hud->stats);
    }
    UiFlex rowFlex{};
    rowFlex.direction = UiFlexDir::Row;
    rowFlex.grow = 1.0f;
    rowFlex.shrink = 1.0f;
    rowFlex.minH = 72.0f;
    const uint16_t row = uiPanel(*canvas.state, root, rowFlex, hiddenPaint());
    UiFlex viewColFlex{};
    viewColFlex.direction = UiFlexDir::Column;
    viewColFlex.grow = 1.0f;
    viewColFlex.shrink = 1.0f;
    viewColFlex.minW = 0.0f;
    const uint16_t viewCol = uiPanel(*canvas.state, row, viewColFlex, hiddenPaint());
    (void)loadToolIcon(canvas, 2, "SelectMode.png");
    (void)loadToolIcon(canvas, 3, "MoveMode.png");
    (void)loadToolIcon(canvas, 4, "RotateMode.png");
    (void)loadToolIcon(canvas, 5, "ScallingMode.png");
    (void)loadToolIcon(canvas, 6, "Camera.png");
    (void)loadToolIcon(canvas, 7, "Snap.png");
    UiFlex toolFlex{};
    toolFlex.direction = UiFlexDir::Row;
    toolFlex.heightMode = UiSize::Px;
    toolFlex.height = 36.0f;
    toolFlex.minH = 36.0f;
    toolFlex.shrink = 0.0f;
    toolFlex.align = UiAlign::Center;
    toolFlex.padL = 6.0f;
    toolFlex.padR = 6.0f;
    toolFlex.padT = 4.0f;
    toolFlex.padB = 4.0f;
    toolFlex.gap = 6.0f;
    const uint16_t toolbar = uiPanel(*canvas.state, viewCol, toolFlex, dockPaint());
    hudBuildStrip(canvas, hud, toolbar, root, look);
    hudBuildView(canvas, hud, root, row, viewCol, look);
}

} // namespace burnhope

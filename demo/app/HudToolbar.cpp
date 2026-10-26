#include "app/Engine.hpp"

#include "app/HudBuild.hpp"
#include "files/Browser.hpp"
#include "platform/PlatformUtil.hpp"
#include "render/SceneFrame.hpp"
#include "ui/rhi/Canvas.hpp"
#include "ui/Style.hpp"
#include "ui/StyleSheet.hpp"
#include "ui/widget/Choice.hpp"
#include "ui/Nodes.hpp"

namespace burnhope {

void sceneHud(Canvas& canvas, void* user) {
    auto* hud = static_cast<HudContext*>(user);
    if (hud == nullptr || (!canvasHasFont(canvas) && !canvasUseFont(canvas, 18.0f)) || canvas.state == nullptr) {
        return;
    }
    hud->shown = 0xff;
    hud->tool = {};
    hud->tool.canvas = &canvas;
    hud->tool.scene = hud->scene;
    hud->stats = {};
    for (uint16_t& radio : hud->viewRadio) {
        radio = kUiNone;
    }
    filesLoadIcons(canvas);
    canvasSetLook(canvas, uiLookInk());
    (void)canvasLoadIcon(canvas, 2, "SelectMode.svg");
    (void)canvasLoadIcon(canvas, 3, "MoveMode.svg");
    (void)canvasLoadIcon(canvas, 4, "RotateMode.svg");
    (void)canvasLoadIcon(canvas, 5, "ScallingMode.svg");
    (void)canvasLoadIcon(canvas, 6, "Camera.svg");
    (void)canvasLoadIcon(canvas, 7, "Snap.svg");
    UiPaint hidden{};
    hidden.a = 0.0f;
    hud->bind.attach(hud->block.scalars, 256, hud->block.flags, 64);
    hud->bind.clear();
    uint16_t details = kUiNone;
    uint16_t statsBody = kUiNone;
    uint16_t files = kUiNone;
    uint16_t detailsInner = kUiNone;
    const PropSlot slots[] = {
        {"toolBar", &hud->toolBar},
        {"spawnBtn", &hud->spawnBtn},
        {"snapBtn0", &hud->snapBtn[0]}, {"snapIcon0", &hud->snapIcon[0]}, {"snapNum0", &hud->snapNum[0]},
        {"snapBtn1", &hud->snapBtn[1]}, {"snapIcon1", &hud->snapIcon[1]}, {"snapNum1", &hud->snapNum[1]},
        {"snapBtn2", &hud->snapBtn[2]}, {"snapIcon2", &hud->snapIcon[2]}, {"snapNum2", &hud->snapNum[2]},
        {"playBtn", &hud->playBtn}, {"playGlyph", &hud->playGlyph}, {"pauseBtn", &hud->pauseBtn},
        {"camBtn", &hud->camBtn}, {"viewBtn", &hud->viewBtn}, {"gearBtn", &hud->gearBtn},
        {"viewPanel", &hud->viewPanel}, {"camPanel", &hud->camPanel}, {"statsPanel", &hud->statsPanel},
        {"view0", &hud->viewRadio[0]}, {"view1", &hud->viewRadio[1]}, {"view2", &hud->viewRadio[2]},
        {"view4", &hud->viewRadio[4]}, {"view5", &hud->viewRadio[5]}, {"view6", &hud->viewRadio[6]},
        {"view7", &hud->viewRadio[7]}, {"view8", &hud->viewRadio[8]}, {"view9", &hud->viewRadio[9]},
        {"view10", &hud->viewRadio[10]}, {"view11", &hud->viewRadio[11]}, {"view12", &hud->viewRadio[12]},
        {"view13", &hud->viewRadio[13]}, {"view14", &hud->viewRadio[14]}, {"view15", &hud->viewRadio[15]},
        {"view16", &hud->viewRadio[16]}, {"view17", &hud->viewRadio[17]}, {"view18", &hud->viewRadio[18]},
        {"view19", &hud->viewRadio[19]},
        {"stat0", &hud->statCheck[0]}, {"stat1", &hud->statCheck[1]}, {"stat2", &hud->statCheck[2]},
        {"stat3", &hud->statCheck[3]}, {"stat4", &hud->statCheck[4]}, {"stat5", &hud->statCheck[5]},
        {"statLine0", &hud->statLine[0]}, {"statLine1", &hud->statLine[1]}, {"statLine2", &hud->statLine[2]},
        {"statLine3", &hud->statLine[3]}, {"statLine4", &hud->statLine[4]}, {"statLine5", &hud->statLine[5]},
        {"overlay", &hud->overlay}, {"viewPane", &hud->viewPane},
        {"camFov", &hud->camFov}, {"camFovNum", &hud->camFovNum},
        {"camNear", &hud->camNear}, {"camFar", &hud->camFar}, {"camSpeed", &hud->camSpeed},
        {"camProj0", &hud->camProj[0]}, {"camProj1", &hud->camProj[1]},
        {"camAxis0", &hud->camAxis[0]}, {"camAxis1", &hud->camAxis[1]}, {"camAxis2", &hud->camAxis[2]},
        {"camAxis3", &hud->camAxis[3]}, {"camAxis4", &hud->camAxis[4]}, {"camAxis5", &hud->camAxis[5]},
        {"actors", &hud->tool.objects}, {"propPage", &hud->tool.propPage},
        {"statsPage", &hud->stats.page}, {"dock", &hud->stats.dock},
        {"details", &details}, {"statsBody", &statsBody}, {"files", &files}, {"detailsInner", &detailsInner},
    };
    const PropCommand commands[] = {
        {"file.new", cmdFileNew, hud},
        {"file.open", cmdFileOpen, hud},
        {"file.save", cmdFileSave, hud},
        {"file.load", cmdFileLoad, hud},
        {"file.exit", cmdFileExit, hud},
        {"scene.duplicate", onDuplicateCmd, hud},
        {"view.stats", onStatsCmd, &hud->stats},
        {"scene.spawn", cmdSpawn, hud},
        {"gizmo.mode", cmdGizmo, hud},
        {"gizmo.space", cmdSpace, hud},
        {"gizmo.pivot", cmdPivot, hud},
        {"snap.toggle", cmdSnapToggle, hud},
        {"snap.move", cmdSnapMove, hud},
        {"snap.turn", cmdSnapTurn, hud},
        {"snap.scale", cmdSnapScale, hud},
        {"scene.play", onPlay, hud},
        {"scene.pause", onPause, hud},
        {"camera.open", onCameraButton, hud},
        {"view.open", onViewButton, hud},
        {"view.set", cmdView, hud},
        {"view.gear", onGear, hud},
    };
    for (const PropSlot& slot : slots) {
        if (slot.id != nullptr) {
            *slot.id = kUiNone;
        }
    }
    (void)uiStyleLoadAsset(*canvas.state, "assets/ui/styles.json");
    (void)propImportAsset(*canvas.state, kUiNone, "assets/ui/editor.json", "frame", hud->bind, slots,
        static_cast<uint16_t>(sizeof slots / sizeof slots[0]), commands,
        static_cast<uint16_t>(sizeof commands / sizeof commands[0]), hidden, onImportFlag, hud);
    uiBindChoice(*canvas.state, "spaceChoice", hud->space, nullptr, nullptr);
    uiBindChoice(*canvas.state, "pivotChoice", hud->pivotSpace, nullptr, nullptr);
    hudWire(canvas, hud, details, statsBody, files, detailsInner);
    hudBuildLauncher(canvas, hud);
}

} // namespace burnhope

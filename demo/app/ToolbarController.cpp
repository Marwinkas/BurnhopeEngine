#include "app/ToolbarController.hpp"

#include "render/SceneBlockFill.hpp"
#include "render/SceneFrame.hpp"
#include "gui/GuiHost.hpp"

#include <cstdio>
#include <iterator>
#include <filesystem>

namespace burnhope {
namespace {

struct DebugView {
    uint8_t mode;
    const char* name;
};

// Mirrors the items dbg0..dbg18 of the debugMenu overlay in shell.json.
constexpr DebugView kDebugViews[] = {
    {0, "Lit"}, {1, "Primitive ID"}, {2, "Normals"}, {4, "GI"}, {5, "Каскады"}, {6, "Контакт"}, {7, "Луч"}, {8, "Прямой"},
    {9, "Альбедо"}, {10, "Солнце"}, {11, "Шероховатость"}, {12, "Металл"}, {13, "AO"}, {14, "Свечение"},
    {15, "ReSTIR Свет"}, {16, "ReSTIR Вес"}, {17, "ReSTIR M"}, {18, "ReSTIR Тень"}, {19, "ReSTIR Индекс"},
};
constexpr const char* kDebugItem[] = {
    "dbg.0", "dbg.1", "dbg.2", "dbg.3", "dbg.4", "dbg.5",
    "dbg.6", "dbg.7", "dbg.8", "dbg.9", "dbg.10", "dbg.11",
    "dbg.12", "dbg.13", "dbg.14", "dbg.15", "dbg.16", "dbg.17",
    "dbg.18",
};
static_assert(std::size(kDebugItem) == std::size(kDebugViews));
constexpr float kSnapStep[3] = {0.5f, 15.0f, 0.1f}; // move (m), rotate (deg), scale
constexpr uint8_t kMenuView[4] = {0, 2, 9, 13}; // View menu entries: Lit, Normals, Albedo, AO
constexpr float kSpeedScale[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr const char* kSpeedName[] = {"0.25x", "0.5x", "1x", "2x", "4x"};
constexpr const char* kToolButton[4] = {"tool.select", "tool.move", "tool.rotate", "tool.scale"};

uint32_t viewIndex(uint8_t mode) {
    for (uint32_t i = 0; i < std::size(kDebugViews); ++i) {
        if (kDebugViews[i].mode == mode) {
            return i;
        }
    }
    return 0;
}

void pickView(ToolbarState& tb, uint32_t index) {
    tb.flags->viewMode = kDebugViews[index % std::size(kDebugViews)].mode;
}

void setTool(ToolbarState& tb, uint8_t tool) {
    tb.tool = tool;
    sceneFrameActorGizmo(tb.scene, tool);
}

void onClick(void* user, uint32_t action) {
    auto& tb = *static_cast<ToolbarState*>(user);
    switch (action) {
    case kActPlay: sceneFramePlay(tb.scene); break;
    case kActPause: sceneFramePause(tb.scene, sceneFrameSim(tb.scene) == 2 ? 0 : 1); break;
    case kActStop: sceneFrameStop(tb.scene); break;
    case kActCameraSpeed:
        tb.speedStep = static_cast<uint8_t>((tb.speedStep + 1) % std::size(kSpeedScale));
        sceneFrameCameraSpeed(tb.scene, tb.baseSpeed * kSpeedScale[tb.speedStep]);
        break;
    case kActProjection: sceneFrameCameraProject(tb.scene, sceneFrameCameraProject(tb.scene) != 0 ? 0 : 1); break;
    case kActToolSelect: case kActToolMove: case kActToolRotate: case kActToolScale:
        setTool(tb, static_cast<uint8_t>(action - kActToolSelect));
        break;
    case kActSnap:
        tb.snap = tb.snap != 0 ? 0 : 1;
        for (uint8_t channel = 0; channel < 3; ++channel) {
            sceneFrameSnap(tb.scene, channel, tb.snap, kSnapStep[channel]);
        }
        break;
    case kActFileNew: sceneFrameBlank(tb.scene); break;
    case kActFileBistro: (void)sceneFrameLoadBhop(tb.scene, "cache/BistroExterior.bhop"); break;
    case kActFileSave: (void)sceneFrameSave(tb.scene, "."); break;
    case kActFileQuit: *tb.quit = true; break;
    case kActAddCube: (void)sceneFrameSpawnCube(tb.scene); break;
    case kActDuplicate: (void)sceneFrameActorDuplicate(tb.scene); break;
    case kActViewLit: case kActViewNormals: case kActViewAlbedo: case kActViewAo:
        tb.flags->viewMode = kMenuView[action - kActViewLit];
        break;
    default:
        if (action >= kActDebugBase && action < kActDebugBase + std::size(kDebugViews)) {
            pickView(tb, action - kActDebugBase);
        }
        break;
    }
}

} // namespace

bool toolbarBind(ToolbarState& tb, GuiHost& gui, SceneFrame* scene, HudFlags& flags, bool& quit) {
    tb.gui = &gui;
    tb.scene = scene;
    tb.flags = &flags;
    tb.quit = &quit;
    tb.baseSpeed = sceneFrameCameraSpeed(tb.scene);
    bool ok = true;
    for (uint32_t action : {kActPlay, kActPause, kActStop, kActSnap, kActCameraSpeed, kActProjection, kActToolSelect, kActToolMove,
                            kActToolRotate, kActToolScale, kActFileNew, kActFileBistro, kActFileSave, kActFileQuit, kActAddCube,
                            kActDuplicate, kActViewLit, kActViewNormals, kActViewAlbedo, kActViewAo}) {
        ok = guiHostBindClick(tb.gui, action, onClick, &tb) && ok;
    }
    for (uint32_t i = 0; i < std::size(kDebugViews); ++i) {
        ok = guiHostBindClick(tb.gui, kActDebugBase + i, onClick, &tb) && ok;
    }
    return ok;
}

void toolbarSync(ToolbarState& tb) {
    for (uint32_t i = 0; i < 4; ++i) {
        guiHostSetActive(tb.gui, kToolButton[i], tb.tool == i);
    }
    const uint8_t sim = sceneFrameSim(tb.scene);
    guiHostSetActive(tb.gui, "bar.play", sim == 1);
    guiHostSetActive(tb.gui, "bar.pause", sim == 2);
    const uint32_t view = viewIndex(tb.flags->viewMode);
    guiHostSetText(tb.gui, "bar.debug", kDebugViews[view].name);
    for (uint32_t i = 0; i < std::size(kDebugViews); ++i) {
        guiHostSetActive(tb.gui, kDebugItem[i], i == view);
        guiHostSetIcon(tb.gui, kDebugItem[i], i == view ? "check" : nullptr);
    }
    guiHostSetActive(tb.gui, "bar.snap", tb.snap != 0);
    guiHostSetText(tb.gui, "bar.proj", sceneFrameCameraProject(tb.scene) != 0 ? "Ortho" : "Persp");
    guiHostSetText(tb.gui, "bar.speed", kSpeedName[tb.speedStep]);
    if (tb.frame++ % 15 == 0) {
        char stats[80];
        std::snprintf(stats, sizeof(stats), "%.0f fps  %.1f ms  GPU %.1f ms  %.1fM tris", sceneFrameFps(tb.scene),
                      sceneFrameFps(tb.scene) > 0.0f ? 1000.0f / sceneFrameFps(tb.scene) : 0.0f, sceneFrameGpuMs(tb.scene),
                      static_cast<double>(sceneFrameTriangles(tb.scene)) / 1.0e6);
        guiHostSetText(tb.gui, "bar.stats", stats);
    }
}

} // namespace burnhope

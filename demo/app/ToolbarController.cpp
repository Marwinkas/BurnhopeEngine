#include "app/ToolbarController.hpp"

#include "app/Engine.hpp"
#include "app/GuiActions.hpp"

#include <cstdio>
#include <iterator>
#include <filesystem>

namespace burnhope {
namespace {

struct DebugView {
    uint8_t mode;
    const char* name;
};

constexpr DebugView kDebugViews[] = {{0, "Lit"}, {2, "Normals"}, {9, "Albedo"}, {13, "AO"}};
constexpr float kSpeedScale[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr const char* kSpeedName[] = {"0.25x", "0.5x", "1x", "2x", "4x"};
constexpr const char* kToolButton[4] = {"bar::tSelect", "bar::tMove", "bar::tRotate", "bar::tScale"};

uint32_t viewIndex(uint8_t mode) {
    for (uint32_t i = 0; i < std::size(kDebugViews); ++i) {
        if (kDebugViews[i].mode == mode) {
            return i;
        }
    }
    return 0;
}

void pickView(Engine& e, uint32_t index) {
    e.editor.flags.viewMode = kDebugViews[index % std::size(kDebugViews)].mode;
}

void setTool(Engine& e, uint8_t tool) {
    e.toolbar.tool = tool;
    sceneFrameActorGizmo(e.scene, tool);
}

void onClick(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    ToolbarState& tb = e.toolbar;
    switch (action) {
    case kActPlay: sceneFramePlay(e.scene); break;
    case kActPause: sceneFramePause(e.scene, sceneFrameSim(e.scene) == 2 ? 0 : 1); break;
    case kActStop: sceneFrameStop(e.scene); break;
    case kActDebugView: pickView(e, viewIndex(e.editor.flags.viewMode) + 1); break;
    case kActCameraSpeed:
        tb.speedStep = static_cast<uint8_t>((tb.speedStep + 1) % std::size(kSpeedScale));
        sceneFrameCameraSpeed(e.scene, tb.baseSpeed * kSpeedScale[tb.speedStep]);
        break;
    case kActProjection: sceneFrameCameraProject(e.scene, sceneFrameCameraProject(e.scene) != 0 ? 0 : 1); break;
    case kActToolSelect: case kActToolMove: case kActToolRotate: case kActToolScale:
        setTool(e, static_cast<uint8_t>(action - kActToolSelect));
        break;
    case kActFileNew: sceneFrameBlank(e.scene); break;
    case kActFileBistro: (void)sceneFrameLoadBhop(e.scene, "cache/BistroExterior.bhop"); break;
    case kActFileSave: (void)sceneFrameSave(e.scene, std::filesystem::current_path().string().c_str()); break;
    case kActFileQuit: e.quit = true; break;
    case kActAddCube: (void)sceneFrameSpawnCube(e.scene); break;
    case kActDuplicate: (void)sceneFrameActorDuplicate(e.scene); break;
    case kActViewLit: case kActViewNormals: case kActViewAlbedo: case kActViewAo: pickView(e, action - kActViewLit); break;
    default: break;
    }
}

} // namespace

bool toolbarBind(Engine& e) {
    e.toolbar.baseSpeed = sceneFrameCameraSpeed(e.scene);
    bool ok = true;
    for (uint32_t action : {kActPlay, kActPause, kActStop, kActDebugView, kActCameraSpeed, kActProjection, kActToolSelect, kActToolMove,
                            kActToolRotate, kActToolScale, kActFileNew, kActFileBistro, kActFileSave, kActFileQuit, kActAddCube,
                            kActDuplicate, kActViewLit, kActViewNormals, kActViewAlbedo, kActViewAo}) {
        ok = guiHostBindClick(e.gui, action, onClick, &e) && ok;
    }
    return ok;
}

void toolbarSync(Engine& e) {
    ToolbarState& tb = e.toolbar;
    for (uint32_t i = 0; i < 4; ++i) {
        guiHostSetActive(e.gui, kToolButton[i], tb.tool == i);
    }
    const uint8_t sim = sceneFrameSim(e.scene);
    guiHostSetActive(e.gui, "bar::play", sim == 1);
    guiHostSetActive(e.gui, "bar::pause", sim == 2);
    guiHostSetText(e.gui, "bar::debug", kDebugViews[viewIndex(e.editor.flags.viewMode)].name);
    guiHostSetText(e.gui, "bar::proj", sceneFrameCameraProject(e.scene) != 0 ? "Ortho" : "Persp");
    char caption[32];
    std::snprintf(caption, sizeof(caption), "Speed %s", kSpeedName[tb.speedStep]);
    guiHostSetText(e.gui, "bar::speed", caption);
    if (tb.frame++ % 15 == 0) {
        char stats[80];
        std::snprintf(stats, sizeof(stats), "%.0f fps  %.1f ms  GPU %.1f ms  %.1fM tris", sceneFrameFps(e.scene),
                      sceneFrameFps(e.scene) > 0.0f ? 1000.0f / sceneFrameFps(e.scene) : 0.0f, sceneFrameGpuMs(e.scene),
                      static_cast<double>(sceneFrameTriangles(e.scene)) / 1.0e6);
        guiHostSetText(e.gui, "bar::stats", stats);
    }
}

} // namespace burnhope

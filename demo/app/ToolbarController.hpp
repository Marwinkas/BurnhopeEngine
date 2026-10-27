#pragma once

#include <cstdint>

namespace burnhope {

// Click action ids of assets/gui/shell.json top bar and menus. Ids are unique across the app because the JSON addresses them by number.
enum ToolbarAction : uint32_t {
    kActPlay = 1,
    kActPause = 2,
    kActStop = 3,
    kActCameraSpeed = 5,
    kActProjection = 6,
    kActToolSelect = 7,
    kActToolMove = 8,
    kActToolRotate = 9,
    kActToolScale = 10,
    kActSnap = 11,
    kActFileNew = 20,
    kActFileBistro = 21,
    kActFileSave = 22,
    kActFileQuit = 23,
    kActAddCube = 30,
    kActDuplicate = 31, // Edit menu and the inspector header
    kActViewLit = 40, // 40..43 pick kDebugViews[action - kActViewLit]
    kActViewNormals = 41,
    kActViewAlbedo = 42,
    kActViewAo = 43,
    kActDebugBase = 100, // 100..118 pick kDebugViews[action - kActDebugBase]
};

struct GuiHost;
struct SceneFrame;
struct HudFlags;

struct ToolbarState {
    uint8_t tool = 0;      // gizmo mode: 0 select, 1 move, 2 rotate, 3 scale
    uint8_t speedStep = 2; // index into the camera speed multipliers
    uint8_t snap = 0;
    uint8_t frame = 0;
    float baseSpeed = 4.0f;
    GuiHost* gui = nullptr; // set by toolbarBind
    SceneFrame* scene = nullptr;
    HudFlags* flags = nullptr;
    bool* quit = nullptr;
};

// Binds the top bar (menus, gizmo modes, play / pause, camera and debug view buttons) of shell.json.
[[nodiscard]] bool toolbarBind(ToolbarState& tb, GuiHost& gui, SceneFrame* scene, HudFlags& flags, bool& quit);
// Every frame: tool / play highlights, button captions and the frame statistics.
void toolbarSync(ToolbarState& tb);

} // namespace burnhope

#pragma once

#include <cstdint>

namespace burnhope {

// Click action ids of assets/gui/inspector*.json. Ids are unique across the app because the JSON addresses them by number.
enum InspectorAction : uint32_t {
    kActStatic = 50,
    kActResetPos = 52,
    kActResetRot = 53,
    kActResetScale = 54,
    kActSunOn = 60,
    kActSunCast = 61,
    kActSunCsm = 62,
    kActSky = 63,
    kActClouds = 64,
    kActClockSun = 65,
    kActBloom = 70,
    kActGtao = 71,
    kActFog = 72,
};

struct GuiHost;
struct SceneFrame;
struct EditorState;
struct InspectorState;

// Which of the nine pose fields / six material fields a drag callback writes.
struct InspectorField {
    InspectorState* state = nullptr;
    uint32_t index = 0;
};

struct InspectorState {
    InspectorField pose[9]{};
    InspectorField pbr[6]{};
    uint8_t shown = 0xff;         // actor the inspector currently displays
    uint8_t lastToggle[16]{};     // last On/Off state pushed to each toggle button (0xff = never)
    GuiHost* gui = nullptr;       // set by inspectorBind
    SceneFrame* scene = nullptr;
    EditorState* editor = nullptr;
};

// Binds the inspector nodes of shell.json to the editor records (post, tweaks, flags) and the selected actor.
[[nodiscard]] bool inspectorBind(InspectorState& ins, GuiHost& gui, SceneFrame* scene, EditorState& editor);
// Every frame: follows the selection, mirrors the actor pose / material and the toggle states into the GUI.
void inspectorSync(InspectorState& ins);

} // namespace burnhope

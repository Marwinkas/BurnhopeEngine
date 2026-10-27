#pragma once

#include <cstdint>

namespace burnhope {

struct Engine;

// Which of the nine pose fields / six material fields a drag callback writes.
struct InspectorField {
    Engine* engine = nullptr;
    uint32_t index = 0;
};

struct InspectorState {
    InspectorField pose[9]{};
    InspectorField pbr[6]{};
    uint8_t shown = 0xff;         // actor the inspector currently displays
    uint8_t lastToggle[16]{};     // last On/Off state pushed to each toggle button (0xff = never)
};

// Binds the inspector nodes of shell.json to the editor records (post, tweaks, flags) and the selected actor.
[[nodiscard]] bool inspectorBind(Engine& e);
// Every frame: follows the selection, mirrors the actor pose / material and the toggle states into the GUI.
void inspectorSync(Engine& e);

} // namespace burnhope

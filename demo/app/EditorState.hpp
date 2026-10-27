#pragma once

#include "render/SceneBlockFill.hpp"
#include "render/SceneFrame.hpp"

namespace burnhope {

// Live scene record: the window block is rebuilt from these four records every frame.
struct EditorState {
    SceneBlock block{};
    HudPost post{};
    SceneTweaks tweaks{};
    HudFlags flags{};
    HudActor actor{};
    uint8_t lookGrab = 0;
    uint8_t toolGrab = 0;
    uint8_t guiHold = 0; // pointer buttons pressed over the GUI and still held
    uint32_t presentSeen = 0;
    float lookX = 0.0f;
    float lookY = 0.0f;
};

} // namespace burnhope

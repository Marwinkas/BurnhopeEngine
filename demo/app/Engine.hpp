#pragma once

#include "app/BrowserController.hpp"
#include "app/InspectorController.hpp"
#include "app/ToolbarController.hpp"
#include "host/GuiHost.hpp"
#include "host/Host.hpp"
#include "render/SceneFrame.hpp"

namespace burnhope {

// Live inspector and post values. Sliders point here when they are created.
struct HudActor {
    float color[3]{1.0f, 1.0f, 1.0f};
    float rough = 0.5f;
    float metal = 0.0f;
    float emissive = 0.0f;
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    float pivot[3]{};
    uint8_t isStatic = 0;
};

// Modes and pass bits that are not fields of HudPost or SceneTweaks.
struct HudFlags {
    uint8_t hiz = 1;
    uint8_t cone = 1;
    uint8_t lod = 1;
    uint8_t frust = 1;
    uint8_t small = 1;
    uint8_t backf = 1;
    uint8_t scrn = 1;
    uint8_t behind = 1;
    uint8_t shadowCull = 1;
    uint8_t probes = 1;
    uint8_t cluster = 1;
    uint8_t points = 1;
    uint8_t spots = 1;
    uint8_t area = 1;
    uint8_t sky = 1;
    uint8_t clouden = 1;
    uint8_t fogen = 0;
    uint8_t bloomen = 1;
    uint8_t contacten = 0;
    uint8_t vt = 0;
    uint8_t lumScale = 0;
    uint8_t gamut = 0;
    uint8_t sun = 1;
    uint8_t sunCast = 1;
    uint8_t wxClock = 0;
    uint8_t tone = 0;
    uint8_t aa = 4;
    uint8_t fsrQ = 0;
    uint8_t giMode = 3;
    uint8_t directMode = 1;
    uint8_t reflectMode = 2;
    uint8_t aoMode = 1;
    uint8_t sunShadow = 0;
    uint8_t viewMode = 0;
    float forceLod = -1.0f;
};

// Live scene record: the window block is rebuilt from these four records every frame.
struct EditorState {
    SceneBlock block{};
    HudPost post{};
    SceneTweaks tweaks{};
    HudFlags flags{};
    HudActor actor{};
    uint8_t lookGrab = 0;
    uint8_t toolGrab = 0;
    uint32_t presentSeen = 0;
    float lookX = 0.0f;
    float lookY = 0.0f;
};

struct Engine {
    Host host;
    SceneFrame* scene = nullptr;
    EditorState editor{};
    GuiHost* gui = nullptr;
    InspectorState inspector{};
    BrowserState browser{};
    ToolbarState toolbar{};
    int slot = -1;
    bool quit = false;
};

[[nodiscard]] bool engineImportScene();
[[nodiscard]] bool engineInit(Engine& e, int argc, char** argv);
void engineShutdown(Engine& e);
[[nodiscard]] bool engineTick(Engine& e);

} // namespace burnhope

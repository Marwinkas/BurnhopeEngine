#pragma once

#include <cstdint>
#include <vector>

#include "asset/MaterialBake.hpp"

namespace burnhope {

// Click action ids of assets/gui/material.json. Ids are unique across the app because the JSON addresses them by number.
enum MaterialAction : uint32_t {
    kActMatClose = 140,
    kActMatFlipY = 141,
    kActMatResetTiling = 142,
    kActMatResetOffset = 143,
    kActMatSave = 144,
    kActMatShape = 145, // 145..147: sphere, cube, plane
};

struct GuiHost;
struct SceneFrame;
struct Canvas;

// The editor saved the .bhmat at `path` (the file browser refreshes its thumbnail).
using MaterialSavedFn = void (*)(void* app, const char* path);
struct MaterialState;

struct MaterialField {
    MaterialState* state = nullptr;
    uint32_t index = 0;
};

// The material editor: the open .bhmat document, its baked map atlas (photo slot 9) and the shaded preview (photo slot 8).
struct MaterialState {
    GuiHost* gui = nullptr; // set by materialBind; MaterialFields.cpp works on this state alone, never on the Engine
    SceneFrame* scene = nullptr;
    void* app = nullptr;
    MaterialSavedFn saved = nullptr;
    BhmatDoc doc{};
    char path[512]{};
    bool open = false;
    bool atlasDirty = false;
    bool previewDirty = false;
    bool sceneDirty = false;      // the document changed and the target actor still shows the old one
    uint8_t target = 0xff;        // actor that shows the document live (kSceneActorNone = none)
    uint8_t shape = 0; // MatShape
    float yaw = 0.0f;
    float pitch = 0.0f;
    MaterialField field[32]{};
    std::vector<uint8_t> atlas;
    std::vector<uint8_t> preview;
};

// Mounts assets/gui/material.json into the main row and binds its cards, sliders, pills and buttons.
[[nodiscard]] bool materialBind(MaterialState& m, GuiHost& gui, SceneFrame* scene, void* app, MaterialSavedFn saved);
// Shows `path` on `actor` (the document is applied to its material at once).
[[nodiscard]] bool materialAssign(MaterialState& m, const char* path, uint8_t actor);
// Loads a .bhmat into the editor and shows its panel. False when the file cannot be read.
[[nodiscard]] bool materialOpen(MaterialState& m, const char* path);
// Re-bakes and uploads the atlas / preview and pushes the document to the target actor when it changed.
void materialSync(MaterialState& m, Canvas* canvas);

} // namespace burnhope

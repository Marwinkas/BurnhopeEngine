#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "asset/MaterialBake.hpp"

namespace burnhope {

// Click action ids of assets/gui/material.json and the popups of assets/gui/menus.json. Ids are unique across the app because
// the JSON addresses them by number.
enum MaterialAction : uint32_t {
    kActMatFlipY = 141,
    kActMatResetTiling = 142,
    kActMatResetOffset = 143,
    kActMatSave = 144,
    kActMatShape = 145,       // 145..147: sphere, cube, plane
    kActMatSlot = 300,        // 300..308: albedo, normal, metal, height, alpha, rough, ao, emission, orm (opens the texture picker)
    kActMatResetScalar = 310, // 310..316: one per kScalars entry
    kActMatTypeOpen = 320,
    kActMatType = 321,        // 321..323: Lit, Unlit, ClearCoat
    kActMatMapsOpen = 324,
    kActMatMaps = 325,        // 325..326: Classic, ORMH
    kActMatDouble = 330,
    kActMatRepeat = 331,
    kActMatTriplanar = 332,
    kActMatSwatch = 340,      // 340..341: albedo, emission (opens the colour picker)
    kActMatResetAlbedo = 342,
    kActMatResetEmission = 343,
    kActMatTexItem = 350,     // item action of the texture picker list
};

struct GuiHost;
struct SceneFrame;
struct Canvas;

// The editor saved the .bhmat at `path` (the file browser refreshes its thumbnail).
using MaterialSavedFn = void (*)(void* app, const char* path);
// Thumbnail cell (1 + atlas index, GuiListEntry::thumb) of the texture file `path`; 0 while it is not baked yet. `budget` counts
// the bakes this frame may still spend.
using MaterialThumbFn = uint8_t (*)(void* app, const char* path, uint32_t& budget);
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
    MaterialThumbFn thumb = nullptr;
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
    bool orbitHeld = false; // the orbit pad has reported the grab point of the gesture in progress
    float orbitX = 0.0f;   // last pad position (0..1) of that gesture
    float orbitY = 0.0f;
    MaterialField field[24]{};
    MaterialField swatch[2]{}; // colour picker targets: albedo, emission
    std::filesystem::path root; // project root: the texture picker lists the textures below it
    std::vector<std::filesystem::path> textures; // every texture below the root, sorted
    std::vector<uint32_t> shown;                 // indices into `textures` of rows 1.. of the picker (the search filter applied)
    std::vector<uint8_t> cells;                  // thumbnail cell shown on each picker row (0 = none yet)
    char search[48]{};                           // text of the picker's search bar
    uint8_t pickSlot = 0xff;    // slot card whose picker is open (index into the kActMatSlot range)
    std::vector<uint8_t> atlas;
    std::vector<uint8_t> preview;
};

// Mounts assets/gui/material.json as a tab of the right dock and binds its cards, sliders, pills and buttons.
[[nodiscard]] bool materialBind(MaterialState& m, GuiHost& gui, SceneFrame* scene, void* app, MaterialSavedFn saved, MaterialThumbFn thumb);
// Shows `path` on `actor` (the document is applied to its material at once).
[[nodiscard]] bool materialAssign(MaterialState& m, const char* path, uint8_t actor);
// Loads a .bhmat into the editor and shows its panel. False when the file cannot be read.
[[nodiscard]] bool materialOpen(MaterialState& m, const char* path);
// Re-bakes and uploads the atlas / preview and pushes the document to the target actor when it changed.
void materialSync(MaterialState& m, Canvas* canvas);

} // namespace burnhope

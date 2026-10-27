#pragma once

#include <cstdint>

#include <filesystem>
#include <string>
#include <vector>

#include "app/BrowserModel.hpp"
#include "app/BrowserThumbs.hpp"
#include "gui/GuiHost.hpp"

namespace burnhope {

// Click action ids of assets/gui/files.json and menus.json. Ids are unique across the app because the JSON addresses them by number.
enum BrowserAction : uint32_t {
    kActFilterBase = 90, // 90..93 pick the file kind filter: All, Textures, Materials, Scenes
    kActFolderUp = 82,
    kActBrowserPage = 160, // new file
    kActBrowserBack = 161,
    kActBrowserForward = 162,
    kActBrowserCreate = 170, // 170..173: new folder, material, scene, file
    kActBrowserSort = 175,
    kActBrowserView = 177, // tile grid <-> compact list
    kActCtxOpen = 180,
    kActCtxReveal = 181,
    kActCtxDuplicate = 182,
    kActCtxDelete = 183,
};

struct SceneFrame;
struct Window;
struct JobPool;

// The material editor behind the browser: open a .bhmat, or show it on an actor (drag and drop).
using BrowserOpenMaterialFn = bool (*)(void* app, const char* path);
using BrowserAssignMaterialFn = bool (*)(void* app, const char* path, uint8_t actor);


// Drag & drop payload of the tiles and tree rows (BrowserDrag.cpp).
struct BrowserPointer {
    std::vector<std::filesystem::path> payload;  // paths carried by the drag that is armed or live
};

struct BrowserState {
    std::filesystem::path root;
    std::filesystem::path cwd;
    std::vector<std::filesystem::path> tree;       // folders listed in the tree, index-aligned with the tree list rows
    std::vector<std::filesystem::path> expanded;   // folders whose children the tree shows
    std::vector<BrowserItem> files;                // tiles of the file grid
    std::vector<GuiListEntry> rows;                // scratch for guiHostSetList
    std::vector<std::filesystem::path> history;    // visited folders, history[historyAt] is the current one
    std::vector<std::filesystem::path> crumbs;     // folder each shown breadcrumb jumps to
    std::vector<std::string> crumbNames;           // display names of the crumbs (storage for crumbText)
    std::vector<const char*> crumbText;            // what guiHostSetCrumbs reads
    size_t historyAt = 0;
    char filter[48]{};
    uint8_t kindFilter = 0; // 0 all, 1 textures, 2 materials, 3 scenes
    SortMode sort = SortMode::Name;
    uint8_t tilePx = 64;
    float tileF = 64.0f;                // tile size before rounding (Ctrl + wheel steps it)
    bool listView = false;              // compact rows instead of the tile grid
    bool importPending = false;         // the import dialog is open
    uint32_t anchor = kGuiNoSelection;  // last plainly clicked tile, the start of Shift ranges
    bool armed = false;                 // left press on empty grid space, marquee not yet started
    bool rubbing = false;               // marquee drag is live
    bool prevLeft = false;
    float rubX = 0.0f;
    float rubY = 0.0f;
    std::vector<uint8_t> base;          // marks the marquee started from (kept with Ctrl)
    uint32_t selected = kGuiNoSelection;
    uint32_t context = kGuiNoSelection; // tile the context menu was opened on
    BrowserPointer pointer;
    ThumbCache thumbs;
    // What the browser reaches in the application, refreshed by browserBind / browserSync. The Browser*.cpp helpers take
    // this struct, never the Engine.
    GuiHost* gui = nullptr;
    SceneFrame* scene = nullptr;
    Canvas* canvas = nullptr;
    Window* window = nullptr; // owner of the native dialogs, refreshed by browserSync
    void* app = nullptr;
    BrowserOpenMaterialFn openMaterial = nullptr;
    BrowserAssignMaterialFn assignMaterial = nullptr;
};

// Binds the project tree, the file grid, the header and the menus; lists the working directory.
[[nodiscard]] bool browserBind(BrowserState& br, GuiHost& gui, SceneFrame* scene, JobPool& jobs, void* app, BrowserOpenMaterialFn openMaterial,
                               BrowserAssignMaterialFn assignMaterial);
// Main thread, once per frame: resolves thumbnails of the tiles in view.
void browserSync(BrowserState& br, Canvas* canvas, Window* window);
// Entity path of the tile grid (the thumbnail cache addresses its rows).
[[nodiscard]] const char* browserGridPath();
// Entity path of the project tree list.
[[nodiscard]] const char* browserTreePath();
// Roots the browser at a new project folder.
void browserSetRoot(BrowserState& br, const std::filesystem::path& dir);
// Rescans the working directory and the tree.
void browserRefresh(BrowserState& br);

} // namespace burnhope

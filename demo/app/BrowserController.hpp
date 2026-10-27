#pragma once

#include <filesystem>
#include <vector>

#include "app/BrowserModel.hpp"
#include "app/BrowserThumbs.hpp"
#include "host/GuiHost.hpp"

namespace burnhope {

struct Engine;

constexpr uint32_t kCrumbCap = 6;

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
    std::filesystem::path crumb[kCrumbCap];        // folder each shown breadcrumb jumps to
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
    Engine* engine = nullptr;
};

// Binds the project tree, the file grid, the header and the menus; lists the working directory.
[[nodiscard]] bool browserBind(Engine& e);
// Main thread, once per frame: resolves thumbnails of the tiles in view.
void browserSync(Engine& e);
// Entity path of the tile grid (the thumbnail cache addresses its rows).
[[nodiscard]] const char* browserGridPath();
// Entity path of the project tree list.
[[nodiscard]] const char* browserTreePath();
// Roots the browser at a new project folder.
void browserSetRoot(Engine& e, const std::filesystem::path& dir);
// Rescans the working directory and the tree.
void browserRefresh(Engine& e);

} // namespace burnhope

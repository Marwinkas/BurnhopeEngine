#pragma once

#include <filesystem>
#include <vector>

#include "app/BrowserModel.hpp"
#include "app/BrowserThumbs.hpp"
#include "host/GuiHost.hpp"

namespace burnhope {

struct Engine;

constexpr uint32_t kCrumbCap = 6;

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
    bool showHidden = false;
    uint8_t tilePx = 64;
    uint32_t selected = kGuiNoSelection;
    uint32_t context = kGuiNoSelection; // tile the context menu was opened on
    ThumbCache thumbs;
    Engine* engine = nullptr;
};

// Binds the project tree, the file grid, the header and the menus; lists the working directory.
[[nodiscard]] bool browserBind(Engine& e);
// Main thread, once per frame: resolves thumbnails of the tiles in view.
void browserSync(Engine& e);
// Entity path of the tile grid (the thumbnail cache addresses its rows).
[[nodiscard]] const char* browserGridPath();
// Roots the browser at a new project folder.
void browserSetRoot(Engine& e, const std::filesystem::path& dir);
// Rescans the working directory and the tree.
void browserRefresh(Engine& e);

} // namespace burnhope

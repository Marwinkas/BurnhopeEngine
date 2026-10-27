#pragma once

#include <filesystem>
#include <vector>

#include "host/GuiHost.hpp"

namespace burnhope {

struct Engine;

// One shown file-grid row: the path behind it and what a double click does with it.
struct BrowserItem {
    std::filesystem::path path;
    uint8_t kind = 0;
};

struct BrowserState {
    std::filesystem::path root;
    std::filesystem::path cwd;
    std::vector<std::filesystem::path> tree; // folders listed in the tree, index-aligned with the tree list rows
    std::vector<BrowserItem> files;          // rows of the file grid
    std::vector<GuiListEntry> rows;          // scratch for guiHostSetList
    char filter[48]{};
    Engine* engine = nullptr;
};

// Binds the project tree, the file grid, the Up button and the search field; lists the working directory.
[[nodiscard]] bool browserBind(Engine& e);

} // namespace burnhope

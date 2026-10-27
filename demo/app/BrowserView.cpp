#include <algorithm>
#include <cstdio>

#include "app/BrowserInternal.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kStatus = "browser.status";
constexpr const char* kDetail = "browser.detail";
constexpr uint8_t kNoThumbSlot = 255;

const char* kindIcon(FileKind kind) {
    switch (kind) {
    case FileKind::Folder: return "folder";
    case FileKind::Scene: return "scene";
    case FileKind::Model: return "file_box";
    case FileKind::Texture: return "file_image";
    case FileKind::Material: return "file_braces";
    default: return "file";
    }
}

GuiListEntry tileRow(const BrowserState& br, const BrowserItem& item) {
    GuiListEntry row{};
    if (br.listView) {
        std::snprintf(row.text, sizeof(row.text), "%s", item.name);
        row.row.icon = guiHostIconCell(kindIcon(item.kind));
        fileDateText(row.row.col1, sizeof(row.row.col1), item.mtime);
        if (item.kind != FileKind::Folder) {
            fileSizeText(row.row.col2, sizeof(row.row.col2), item.size);
        }
    } else {
        std::snprintf(row.text, sizeof(row.text), "%s\n%s", item.name, fileKindCaption(item.kind));
    }
    row.mark = item.marked ? 1 : 0;
    row.thumb = item.cell;
    return row;
}

// Appends `dir` as a tree row (depth = path components below the project root) and, when unfolded, its listed subfolders.
void scanTree(BrowserState& b, const fs::path& dir, uint32_t depth) {
    const bool open = browserContains(b.expanded, dir);
    const bool branch = dirHasFolders(dir);
    b.tree.push_back(dir);
    GuiListEntry row{};
    std::snprintf(row.text, sizeof(row.text), "%s", dir.filename().string().c_str());
    row.row.depth = static_cast<uint8_t>(depth);
    row.row.flags = gui::kGuiRowTree;
    row.row.chevron = branch ? guiHostIconCell(open ? "chevron_down" : "chevron_right") : 0;
    row.row.icon = guiHostIconCell("folder");
    b.rows.push_back(row);
    if (!branch || !open) {
        return;
    }
    std::vector<fs::path> children;
    std::error_code ec;
    for (const fs::directory_entry& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (entry.is_directory(ec) && dirListed(entry.path())) {
            children.push_back(entry.path());
        }
    }
    std::sort(children.begin(), children.end());
    for (const fs::path& child : children) {
        scanTree(b, child, depth + 1);
    }
}

// Breadcrumb strip: one crumb per folder from the project root to the working directory.
void showCrumbs(BrowserState& br) {
    BrowserState& b = br;
    b.crumbs.clear();
    std::error_code ec;
    const fs::path rel = fs::relative(b.cwd, b.root, ec);
    fs::path at = b.root;
    b.crumbs.push_back(at);
    if (!ec && rel != ".") {
        for (const fs::path& part : rel) {
            at /= part;
            b.crumbs.push_back(at);
        }
    }
    b.crumbNames.clear();
    for (size_t i = 0; i < b.crumbs.size(); ++i) {
        b.crumbNames.push_back((i == 0 ? b.root.filename() : b.crumbs[i].filename()).string());
    }
    b.crumbText.clear();
    for (const std::string& name : b.crumbNames) {
        b.crumbText.push_back(name.c_str());
    }
    (void)guiHostSetCrumbs(br.gui, kBrowserPill, b.crumbText.data(), static_cast<uint32_t>(b.crumbText.size()));
}

} // namespace

bool browserContains(const std::vector<fs::path>& list, const fs::path& p) {
    return std::find(list.begin(), list.end(), p) != list.end();
}

uint32_t browserDepthOf(const BrowserState& b, const fs::path& dir) {
    uint32_t depth = 0;
    for (const fs::path& part : dir.lexically_relative(b.root)) {
        depth += part != "." ? 1u : 0u;
    }
    return depth;
}

void browserShowTree(BrowserState& br) {
    BrowserState& b = br;
    b.tree.clear();
    b.rows.clear();
    if (!browserContains(b.expanded, b.root)) {
        b.expanded.push_back(b.root);
    }
    scanTree(b, b.root, 0);
    guiHostSetList(br.gui, kBrowserTree, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    for (uint32_t i = 0; i < b.tree.size(); ++i) {
        if (b.tree[i] == b.cwd) {
            guiHostSetListSelected(br.gui, kBrowserTree, i);
        }
    }
}

void browserShowStatus(BrowserState& br) {
    BrowserState& b = br;
    uint32_t folders = 0;
    uint32_t marked = 0;
    uintmax_t bytes = 0;
    for (const BrowserItem& item : b.files) {
        folders += item.kind == FileKind::Folder ? 1u : 0u;
        bytes += item.kind == FileKind::Folder ? 0 : item.size;
        marked += item.marked ? 1u : 0u;
    }
    const uint32_t files = static_cast<uint32_t>(b.files.size()) - folders;
    char size[32];
    fileSizeText(size, sizeof(size), bytes);
    char text[128];
    std::snprintf(text, sizeof(text), "%u %s, %u %s (%s)", folders, folders == 1 ? "folder" : "folders", files, files == 1 ? "file" : "files",
                  size);
    guiHostSetText(br.gui, kStatus, text);
    if (marked > 1) {
        std::snprintf(text, sizeof(text), "Selected: %u items", marked);
    } else if (b.selected < b.files.size()) {
        const BrowserItem& item = b.files[b.selected];
        fileSizeText(size, sizeof(size), item.size);
        const bool folder = item.kind == FileKind::Folder;
        std::snprintf(text, sizeof(text), "Selected: %s%s%s%s", item.path.filename().string().c_str(), folder ? "" : " (", folder ? "" : size,
                      folder ? "" : ")");
    } else {
        text[0] = '\0';
    }
    guiHostSetText(br.gui, kDetail, text);
}

void browserShowFiles(BrowserState& br) {
    BrowserState& b = br;
    browserScan(b.cwd, BrowserQuery{b.filter, b.kindFilter, b.sort}, b.files);
    b.rows.clear();
    for (const BrowserItem& item : b.files) {
        b.rows.push_back(tileRow(br, item));
    }
    b.selected = kGuiNoSelection;
    b.anchor = kGuiNoSelection;
    guiHostSetList(br.gui, kBrowserGrid, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    showCrumbs(br);
    browserShowStatus(br);
}

void browserApplyTile(BrowserState& br) {
    BrowserState& b = br;
    if (b.listView) {
        guiHostSetListItemClass(br.gui, kBrowserGrid, "row_item");
        guiHostSetListThumbs(br.gui, kBrowserGrid, kNoThumbSlot, 0, 0, 0);
        guiHostSetListMetrics(br.gui, kBrowserGrid, 0.0f, guiHostToken(br.gui, "list.rowH"), false);
        return;
    }
    const float tile = static_cast<float>(b.tilePx);
    guiHostSetListItemClass(br.gui, kBrowserGrid, "tile_item");
    guiHostSetListThumbs(br.gui, kBrowserGrid, kThumbSlot, kThumbGrid, kThumbGrid, b.tilePx);
    guiHostSetListMetrics(br.gui, kBrowserGrid, tile + guiHostToken(br.gui, "tile.gutter"), tile + guiHostToken(br.gui, "tile.caption"), true);
}

// Re-renders the tile captions (zoom, view switch) in place: scroll position and marks stay.
void browserRestyleTiles(BrowserState& br) {
    BrowserState& b = br;
    browserApplyTile(br);
    b.rows.clear();
    for (const BrowserItem& item : b.files) {
        b.rows.push_back(tileRow(br, item));
    }
    guiHostUpdateList(br.gui, kBrowserGrid, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
}

void browserEnter(BrowserState& br, const fs::path& dir, bool record) {
    BrowserState& b = br;
    if (record && (b.history.empty() || b.history[b.historyAt] != dir)) {
        b.history.resize(b.historyAt + 1);
        b.history.push_back(dir);
        b.historyAt = b.history.size() - 1;
    }
    b.cwd = dir;
    browserShowFiles(br);
    // The tree unfolds every parent of the folder (the folder itself stays as it was) and lights the active row.
    for (fs::path up = dir.parent_path(); !up.lexically_relative(b.root).empty() && *up.lexically_relative(b.root).begin() != ".."; up = up.parent_path()) {
        if (!browserContains(b.expanded, up)) {
            b.expanded.push_back(up);
        }
        if (up == b.root) {
            break;
        }
    }
    browserShowTree(br);
}

} // namespace burnhope

#include "app/BrowserController.hpp"

#include "app/BrowserFiles.hpp"
#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "gpu_scene/SceneObject.hpp"
#include "platform/Desktop.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kTree = "bottom::treeCol::tree";
constexpr const char* kGrid = "bottom::filesCol::grid";
constexpr const char* kBar = "bottom::filesCol::bar::";
constexpr const char* kPill = "bottom::filesCol::bar::pill::";
constexpr const char* kStatus = "bottom::filesCol::info::status";
constexpr const char* kDetail = "bottom::filesCol::info::detail";
constexpr const char* kFilterName[] = {"All", "Textures", "Materials", "Scenes", "Models"};
constexpr const char* kSortName[] = {"Sort: Name", "Sort: Size", "Sort: Type", "Sort: Date"};
constexpr uint32_t kTreeCap = 400;
constexpr uint32_t kNameShown = 14;
constexpr float kTileGutter = 44.0f;
constexpr float kTileCaption = 50.0f;

bool contains(const std::vector<fs::path>& list, const fs::path& p) {
    return std::find(list.begin(), list.end(), p) != list.end();
}

void path(char* out, size_t cap, const char* base, const char* leaf) {
    std::snprintf(out, cap, "%s%s", base, leaf);
}

// Cuts `name` to `kNameShown` characters without splitting a UTF-8 sequence.
std::string shortName(const std::string& name) {
    uint32_t chars = 0;
    size_t at = 0;
    while (at < name.size() && chars < kNameShown) {
        ++at;
        while (at < name.size() && (static_cast<unsigned char>(name[at]) & 0xC0) == 0x80) {
            ++at;
        }
        ++chars;
    }
    return at >= name.size() ? name : name.substr(0, at) + "..";
}

GuiListEntry tileRow(const BrowserItem& item, bool selectedTone) {
    GuiListEntry row{};
    const std::string text = shortName(item.path.filename().string()) + "\n" + fileKindCaption(item.kind);
    std::snprintf(row.text, sizeof(row.text), "%s", text.c_str());
    row.tone = selectedTone ? 1 : 0;
    return row;
}

void scanTree(BrowserState& b, const fs::path& dir, uint32_t depth) {
    if (b.tree.size() >= kTreeCap) {
        return;
    }
    const bool open = contains(b.expanded, dir);
    const bool branch = dirHasFolders(dir);
    b.tree.push_back(dir);
    GuiListEntry row{};
    const std::string name = depth == 0 && dir == b.root ? dir.filename().string() : dir.filename().string();
    std::snprintf(row.text, sizeof(row.text), "%s%s %s", std::string(depth * 2, ' ').c_str(), branch ? (open ? "v" : ">") : " ", name.c_str());
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

void showTree(Engine& e) {
    BrowserState& b = e.browser;
    b.tree.clear();
    b.rows.clear();
    if (!contains(b.expanded, b.root)) {
        b.expanded.push_back(b.root);
    }
    scanTree(b, b.root, 0);
    guiHostSetList(e.gui, kTree, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    for (uint32_t i = 0; i < b.tree.size(); ++i) {
        if (b.tree[i] == b.cwd) {
            guiHostSetListSelected(e.gui, kTree, i);
        }
    }
}

// Breadcrumb pill: the folders from the project root to the working directory, the last kCrumbCap of them.
void showCrumbs(Engine& e) {
    BrowserState& b = e.browser;
    std::vector<fs::path> chain;
    std::error_code ec;
    const fs::path rel = fs::relative(b.cwd, b.root, ec);
    fs::path at = b.root;
    chain.push_back(at);
    if (!ec && rel != ".") {
        for (const fs::path& part : rel) {
            at /= part;
            chain.push_back(at);
        }
    }
    const size_t skip = chain.size() > kCrumbCap ? chain.size() - kCrumbCap : 0;
    char node[96];
    for (uint32_t i = 0; i < kCrumbCap; ++i) {
        const bool live = skip + i < chain.size();
        std::snprintf(node, sizeof(node), "%sc%u", kPill, i);
        guiHostSetShown(e.gui, node, live);
        if (live) {
            b.crumb[i] = chain[skip + i];
            guiHostSetText(e.gui, node, (skip + i == 0 ? b.root.filename() : chain[skip + i].filename()).string().c_str());
        }
        if (i + 1 < kCrumbCap) {
            std::snprintf(node, sizeof(node), "%ss%u", kPill, i);
            guiHostSetShown(e.gui, node, skip + i + 1 < chain.size());
        }
    }
}

void showStatus(Engine& e) {
    BrowserState& b = e.browser;
    uint32_t folders = 0;
    uintmax_t bytes = 0;
    for (const BrowserItem& item : b.files) {
        if (item.kind == FileKind::Folder) {
            ++folders;
        } else {
            bytes += item.size;
        }
    }
    const uint32_t files = static_cast<uint32_t>(b.files.size()) - folders;
    char size[32];
    fileSizeText(size, sizeof(size), bytes);
    char text[128];
    std::snprintf(text, sizeof(text), "%u %s, %u %s (%s)", folders, pluralRu(folders, "папка", "папки", "папок"), files,
                  pluralRu(files, "файл", "файла", "файлов"), size);
    guiHostSetText(e.gui, kStatus, text);
    if (b.selected < b.files.size()) {
        const BrowserItem& item = b.files[b.selected];
        fileSizeText(size, sizeof(size), item.size);
        const bool folder = item.kind == FileKind::Folder;
        std::snprintf(text, sizeof(text), "%s (%s%s%s)", item.path.filename().string().c_str(), fileKindCaption(item.kind), folder ? "" : ", ",
                      folder ? "" : size);
        guiHostSetText(e.gui, kDetail, text);
    } else {
        guiHostSetText(e.gui, kDetail, "");
    }
}

void showFiles(Engine& e) {
    BrowserState& b = e.browser;
    const BrowserQuery query{b.filter, b.kindFilter, b.sort, b.showHidden};
    browserScan(b.cwd, query, b.files);
    b.rows.clear();
    for (const BrowserItem& item : b.files) {
        b.rows.push_back(tileRow(item, item.kind == FileKind::Folder));
    }
    b.selected = kGuiNoSelection;
    guiHostSetList(e.gui, kGrid, b.rows.data(), static_cast<uint32_t>(b.rows.size()));
    showCrumbs(e);
    showStatus(e);
}

void applyTile(Engine& e) {
    const float tile = static_cast<float>(e.browser.tilePx);
    guiHostSetListThumbs(e.gui, kGrid, kThumbSlot, kThumbGrid, kThumbGrid, e.browser.tilePx);
    guiHostSetListMetrics(e.gui, kGrid, tile + kTileGutter, tile + kTileCaption, true);
}

void enter(Engine& e, const fs::path& dir, bool record) {
    BrowserState& b = e.browser;
    if (record && (b.history.empty() || b.history[b.historyAt] != dir)) {
        b.history.resize(b.historyAt + 1);
        b.history.push_back(dir);
        b.historyAt = b.history.size() - 1;
    }
    b.cwd = dir;
    showFiles(e);
    for (uint32_t i = 0; i < b.tree.size(); ++i) {
        if (b.tree[i] == dir) {
            guiHostSetListSelected(e.gui, kTree, i);
            return;
        }
    }
    guiHostSetListSelected(e.gui, kTree, kGuiNoSelection);
}

void open(Engine& e, const BrowserItem& item) {
    const std::string full = item.path.string();
    uint8_t first = 0;
    uint8_t after = 0;
    const uint8_t actor = sceneFrameActorSelected(e.scene);
    switch (item.kind) {
    case FileKind::Folder: enter(e, item.path, true); break;
    case FileKind::Scene:
        if (item.path.extension() == ".bscene") {
            (void)sceneFrameLoad(e.scene, e.browser.root.string().c_str(), full.c_str());
        } else {
            (void)sceneFrameLoadBhop(e.scene, full.c_str());
        }
        break;
    case FileKind::Model: (void)sceneFrameImportModel(e.scene, e.browser.root.string().c_str(), full.c_str(), first, after); break;
    case FileKind::Texture:
        if (actor < sceneFrameActorCount(e.scene)) {
            sceneFrameQueueTexture(e.scene, actor, 0, full.c_str());
        }
        break;
    case FileKind::Material: (void)materialOpen(e, full.c_str()); break;
    default: break;
    }
}

void onTree(void* user, uint32_t index, bool activate) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (index >= b.tree.size()) {
        return;
    }
    const fs::path dir = b.tree[index];
    const bool here = dir == b.cwd;
    auto at = std::find(b.expanded.begin(), b.expanded.end(), dir);
    // A click opens the folder and unfolds it; clicking the open folder again (or double clicking) folds it.
    if (at == b.expanded.end()) {
        b.expanded.push_back(dir);
    } else if (here || activate) {
        b.expanded.erase(at);
    }
    enter(e, dir, !here);
    showTree(e);
}

void onFile(void* user, uint32_t index, bool activate) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (index >= b.files.size()) {
        return;
    }
    b.selected = index;
    guiHostSetListSelected(e.gui, kGrid, index);
    showStatus(e);
    if (activate) {
        open(e, b.files[index]);
    }
}

void onContext(void* user, uint32_t index) {
    auto& e = *static_cast<Engine*>(user);
    if (index < e.browser.files.size()) {
        e.browser.context = index;
        onFile(user, index, false);
        guiHostOpenAtPointer(e.gui, "ctxMenu");
    }
}

void onFilter(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    e.browser.kindFilter = static_cast<uint8_t>(action - kActFilterBase);
    char node[96];
    path(node, sizeof(node), kBar, "filter");
    guiHostSetText(e.gui, node, kFilterName[e.browser.kindFilter]);
    showFiles(e);
}

void onSearch(void* user, const char* text) {
    auto& e = *static_cast<Engine*>(user);
    std::snprintf(e.browser.filter, sizeof(e.browser.filter), "%s", text);
    showFiles(e);
}

void onNav(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (action == kActFolderUp) {
        if (b.cwd != b.root && b.cwd.has_parent_path()) {
            enter(e, b.cwd.parent_path(), true);
        }
    } else if (action == kActBrowserBack && b.historyAt > 0) {
        enter(e, b.history[--b.historyAt], false);
    } else if (action == kActBrowserForward && b.historyAt + 1 < b.history.size()) {
        enter(e, b.history[++b.historyAt], false);
    } else if (action >= kActBrowserCrumb && action < kActBrowserCrumb + kCrumbCap) {
        enter(e, b.crumb[action - kActBrowserCrumb], true);
    }
}

void created(Engine& e, const fs::path& made) {
    if (made.empty()) {
        return;
    }
    showFiles(e);
    showTree(e);
    for (uint32_t i = 0; i < e.browser.files.size(); ++i) {
        if (e.browser.files[i].path == made) {
            onFile(&e, i, false);
        }
    }
}

void onCreate(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    const NewFile what = action == kActBrowserPage ? NewFile::Plain : static_cast<NewFile>(action - kActBrowserCreate);
    created(e, browserCreate(e.browser.cwd, what));
}

void onTune(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (action == kActBrowserSort) {
        b.sort = static_cast<SortMode>((static_cast<uint32_t>(b.sort) + 1u) % 4u);
        guiHostSetText(e.gui, "tuneMenu::sort", kSortName[static_cast<uint32_t>(b.sort)]);
    } else {
        b.showHidden = !b.showHidden;
        guiHostSetText(e.gui, "tuneMenu::hidden", b.showHidden ? "Hidden: On" : "Hidden: Off");
        guiHostSetActive(e.gui, "tuneMenu::hidden", b.showHidden);
        showTree(e);
    }
    showFiles(e);
}

void onTileSize(void* user, float value) {
    auto& e = *static_cast<Engine*>(user);
    e.browser.tilePx = static_cast<uint8_t>(value);
    applyTile(e);
}

void onContextAction(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (b.context >= b.files.size()) {
        return;
    }
    const BrowserItem item = b.files[b.context];
    b.context = kGuiNoSelection;
    if (action == kActCtxOpen) {
        open(e, item);
    } else if (action == kActCtxReveal) {
        const std::string full = item.path.string();
        const char* one = full.c_str();
        desktopReveal(&one, 1);
    } else if (action == kActCtxDuplicate) {
        created(e, browserDuplicate(item.path));
    } else if (browserTrash(b.root, item.path)) {
        thumbsInvalidate(e, item.path.string().c_str());
        showFiles(e);
        showTree(e);
    }
}

} // namespace

const char* browserGridPath() {
    return kGrid;
}

void browserSetRoot(Engine& e, const fs::path& dir) {
    BrowserState& b = e.browser;
    b.root = dir;
    b.expanded.clear();
    b.history.assign(1, dir);
    b.historyAt = 0;
    enter(e, dir, false);
    showTree(e);
}

void browserRefresh(Engine& e) {
    showFiles(e);
    showTree(e);
}

void browserSync(Engine& e) {
    thumbsSync(e);
}

bool browserBind(Engine& e) {
    std::error_code ec;
    BrowserState& b = e.browser;
    b.engine = &e;
    b.root = fs::current_path(ec);
    b.cwd = b.root;
    b.history.push_back(b.cwd);
    bool ok = thumbsInit(b.thumbs) && guiHostBindList(e.gui, kTree, onTree, &e) && guiHostBindList(e.gui, kGrid, onFile, &e) &&
              guiHostBindListContext(e.gui, kGrid, onContext, &e);
    char node[96];
    path(node, sizeof(node), kBar, "search");
    ok = guiHostBindEdit(e.gui, node, onSearch, &e) && ok;
    ok = guiHostBindDrag(e.gui, "tuneMenu::size", onTileSize, &e) && ok;
    for (uint32_t i = 0; i < 5; ++i) {
        ok = guiHostBindClick(e.gui, kActFilterBase + i, onFilter, &e) && ok;
    }
    for (const uint32_t action : {kActFolderUp, kActBrowserBack, kActBrowserForward}) {
        ok = guiHostBindClick(e.gui, action, onNav, &e) && ok;
    }
    for (uint32_t i = 0; i < kCrumbCap; ++i) {
        ok = guiHostBindClick(e.gui, kActBrowserCrumb + i, onNav, &e) && ok;
    }
    for (uint32_t i = 0; i < 4; ++i) {
        ok = guiHostBindClick(e.gui, kActBrowserCreate + i, onCreate, &e) && ok;
    }
    ok = guiHostBindClick(e.gui, kActBrowserPage, onCreate, &e) && ok;
    ok = guiHostBindClick(e.gui, kActBrowserSort, onTune, &e) && guiHostBindClick(e.gui, kActBrowserHidden, onTune, &e) && ok;
    for (const uint32_t action : {kActCtxOpen, kActCtxReveal, kActCtxDuplicate, kActCtxDelete}) {
        ok = guiHostBindClick(e.gui, action, onContextAction, &e) && ok;
    }
    applyTile(e);
    showTree(e);
    showFiles(e);
    return ok;
}

} // namespace burnhope

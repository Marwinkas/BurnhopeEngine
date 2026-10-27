#include "app/BrowserController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/BrowserDrag.hpp"
#include "app/BrowserFiles.hpp"
#include "app/BrowserInternal.hpp"
#include "gui/GuiHost.hpp"
#include "platform/Jobs.hpp"
#include "platform/Desktop.hpp"
#include "platform/Dialog.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kFilterName[] = {"All", "Textures", "Materials", "Scenes", "Models"};
constexpr const char* kSortName[] = {"Sort: Name", "Sort: Size", "Sort: Type", "Sort: Date"};
constexpr int kImportCap = 16384;

void onFilter(void* user, uint32_t action) {
    auto& br = *static_cast<BrowserState*>(user);
    br.kindFilter = static_cast<uint8_t>(action - kActFilterBase);
    guiHostSetText(br.gui, kBrowserFilter, kFilterName[br.kindFilter]);
    browserShowFiles(br);
}

void onSearch(void* user, const char* text) {
    auto& br = *static_cast<BrowserState*>(user);
    std::snprintf(br.filter, sizeof(br.filter), "%s", text);
    browserShowFiles(br);
}

void onNav(void* user, uint32_t action) {
    auto& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (action == kActFolderUp) {
        if (b.cwd != b.root && b.cwd.has_parent_path()) {
            browserEnter(br, b.cwd.parent_path(), true);
        }
    } else if (action == kActBrowserBack && b.historyAt > 0) {
        browserEnter(br, b.history[--b.historyAt], false);
    } else if (action == kActBrowserForward && b.historyAt + 1 < b.history.size()) {
        browserEnter(br, b.history[++b.historyAt], false);
    }
}

void onCrumb(void* user, uint32_t index) {
    BrowserState& b = *static_cast<BrowserState*>(user);
    if (index < b.crumbs.size()) {
        const fs::path dir = b.crumbs[index]; // browserEnter rebuilds the crumbs
        browserEnter(b, dir, true);
    }
}

void created(BrowserState& br, const fs::path& made) {
    if (made.empty()) {
        return;
    }
    browserShowFiles(br);
    browserShowTree(br);
    for (uint32_t i = 0; i < br.files.size(); ++i) {
        if (br.files[i].path == made) {
            browserOnFile(&br, i, false);
        }
    }
}

void onCreate(void* user, uint32_t action) {
    auto& br = *static_cast<BrowserState*>(user);
    created(br, browserCreate(br.cwd, static_cast<NewFile>(action - kActBrowserCreate)));
}

// Second header button: the native open dialog; browserSync copies the chosen files in when it closes.
void onImport(void* user, uint32_t) {
    auto& br = *static_cast<BrowserState*>(user);
    if (windowAskFile(*br.window, nullptr, 0, FileAsk::OpenMany)) {
        br.importPending = true;
    }
}

void pollImport(BrowserState& br) {
    BrowserState& b = br;
    if (!b.importPending) {
        return;
    }
    char picked[kImportCap];
    const int count = windowTakeFiles(picked, kImportCap);
    if (count <= 0) {
        return;
    }
    b.importPending = false;
    fs::path last;
    const char* at = picked;
    for (int i = 0; i < count; ++i) {
        const fs::path made = browserImport(b.cwd, at);
        last = made.empty() ? last : made;
        at += std::strlen(at) + 1;
    }
    created(br, last);
}

void onTune(void* user, uint32_t action) {
    auto& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (action == kActBrowserView) {
        b.listView = !b.listView;
        guiHostSetText(br.gui, "browser.view", b.listView ? "View: List" : "View: Grid");
        browserRestyleTiles(br);
        return;
    }
    if (action == kActBrowserSort) {
        b.sort = static_cast<SortMode>((static_cast<uint32_t>(b.sort) + 1u) % 4u);
        guiHostSetText(br.gui, "browser.sort", kSortName[static_cast<uint32_t>(b.sort)]);
    }
    browserShowFiles(br);
}

void setTile(BrowserState& br, float px) {
    BrowserState& b = br;
    b.tileF = std::clamp(px, guiHostToken(br.gui, "tile.min"), guiHostToken(br.gui, "tile.max"));
    const uint8_t rounded = static_cast<uint8_t>(std::lround(b.tileF));
    if (rounded != b.tilePx) {
        b.tilePx = rounded;
        browserRestyleTiles(br);
    }
}

void onTileSize(void* user, float value) {
    setTile(*static_cast<BrowserState*>(user), value);
}

void onContextAction(void* user, uint32_t action) {
    auto& br = *static_cast<BrowserState*>(user);
    BrowserState& b = br;
    if (b.context >= b.files.size()) {
        return;
    }
    const BrowserItem item = b.files[b.context];
    b.context = kGuiNoSelection;
    if (action == kActCtxOpen) {
        browserOpen(br, item);
    } else if (action == kActCtxReveal) {
        const std::string full = item.path.string();
        const char* one = full.c_str();
        desktopReveal(&one, 1);
    } else if (action == kActCtxDuplicate) {
        created(br, browserDuplicate(item.path));
    } else if (browserTrash(b.root, item.path)) {
        thumbsInvalidate(br, item.path.string().c_str());
        browserShowFiles(br);
        browserShowTree(br);
    }
}

} // namespace

const char* browserGridPath() {
    return kBrowserGrid;
}

const char* browserTreePath() {
    return kBrowserTree;
}

void browserSetRoot(BrowserState& br, const fs::path& dir) {
    BrowserState& b = br;
    b.root = dir;
    b.expanded.clear();
    b.history.assign(1, dir);
    b.historyAt = 0;
    browserEnter(br, dir, false);
    browserShowTree(br);
}

void browserRefresh(BrowserState& br) {
    browserShowFiles(br);
    browserShowTree(br);
}

void browserSync(BrowserState& br, Canvas* canvas, Window* window) {
    br.canvas = canvas;
    br.window = window;
    thumbsSync(br);
    const float wheel = guiHostTakeZoom(br.gui);
    if (wheel != 0.0f && !br.listView) {
        setTile(br, br.tileF + wheel * guiHostToken(br.gui, "tile.step"));
        guiHostSetValue(br.gui, "browser.size", br.tileF);
    }
    pollImport(br);
    browserPollMarquee(br);
    browserPointerSync(br);
}

bool browserBind(BrowserState& br, GuiHost& gui, SceneFrame* scene, JobPool& jobs, void* app, BrowserOpenMaterialFn openMaterial,
                 BrowserAssignMaterialFn assignMaterial) {
    std::error_code ec;
    BrowserState& b = br;
    b.gui = &gui;
    b.scene = scene;
    b.app = app;
    b.openMaterial = openMaterial;
    b.assignMaterial = assignMaterial;
    b.root = fs::current_path(ec);
    b.cwd = b.root;
    b.history.push_back(b.cwd);
    bool ok = thumbsInit(b.thumbs, jobs) && guiHostBindList(br.gui, kBrowserTree, browserOnTree, &br) && guiHostBindList(br.gui, kBrowserGrid, browserOnFile, &br) &&
              guiHostBindListContext(br.gui, kBrowserGrid, browserOnContext, &br);
    ok = guiHostBindEdit(br.gui, kBrowserSearch, onSearch, &br) && ok;
    ok = guiHostBindDrag(br.gui, "browser.size", onTileSize, &br) && ok;
    for (uint32_t i = 0; i < 5; ++i) {
        ok = guiHostBindClick(br.gui, kActFilterBase + i, onFilter, &br) && ok;
    }
    for (const uint32_t action : {kActFolderUp, kActBrowserBack, kActBrowserForward}) {
        ok = guiHostBindClick(br.gui, action, onNav, &br) && ok;
    }
    ok = guiHostBindCrumbs(br.gui, kBrowserPill, onCrumb, &br) && ok;
    for (uint32_t i = 0; i < 4; ++i) {
        ok = guiHostBindClick(br.gui, kActBrowserCreate + i, onCreate, &br) && ok;
    }
    ok = guiHostBindClick(br.gui, kActBrowserPage, onImport, &br) && ok;
    for (const uint32_t action : {kActBrowserSort, kActBrowserView}) {
        ok = guiHostBindClick(br.gui, action, onTune, &br) && ok;
    }
    guiHostSetListZoom(br.gui, kBrowserGrid, true);
    ok = browserDragBind(br) && ok;
    guiHostSetPlaceholder(br.gui, kBrowserSearch, "Search...");
    guiHostSetValue(br.gui, "browser.size", br.tileF);
    for (const uint32_t action : {kActCtxOpen, kActCtxReveal, kActCtxDuplicate, kActCtxDelete}) {
        ok = guiHostBindClick(br.gui, action, onContextAction, &br) && ok;
    }
    browserApplyTile(br);
    browserShowTree(br);
    browserShowFiles(br);
    return ok;
}

} // namespace burnhope

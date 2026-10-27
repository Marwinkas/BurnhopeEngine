#include "app/BrowserController.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "app/BrowserDrag.hpp"
#include "app/BrowserFiles.hpp"
#include "app/BrowserInternal.hpp"
#include "app/Engine.hpp"
#include "app/GuiActions.hpp"
#include "platform/Desktop.hpp"
#include "platform/Window.hpp"

namespace burnhope {
namespace fs = std::filesystem;
namespace {

constexpr const char* kFilterName[] = {"All", "Textures", "Materials", "Scenes", "Models"};
constexpr const char* kSortName[] = {"Sort: Name", "Sort: Size", "Sort: Type", "Sort: Date"};
constexpr int kImportCap = 16384;

void path(char* out, size_t cap, const char* base, const char* leaf) {
    std::snprintf(out, cap, "%s%s", base, leaf);
}

void onFilter(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    e.browser.kindFilter = static_cast<uint8_t>(action - kActFilterBase);
    char node[96];
    path(node, sizeof(node), kBrowserBar, "filter");
    guiHostSetText(e.gui, node, kFilterName[e.browser.kindFilter]);
    browserShowFiles(e);
}

void onSearch(void* user, const char* text) {
    auto& e = *static_cast<Engine*>(user);
    std::snprintf(e.browser.filter, sizeof(e.browser.filter), "%s", text);
    browserShowFiles(e);
}

void onNav(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (action == kActFolderUp) {
        if (b.cwd != b.root && b.cwd.has_parent_path()) {
            browserEnter(e, b.cwd.parent_path(), true);
        }
    } else if (action == kActBrowserBack && b.historyAt > 0) {
        browserEnter(e, b.history[--b.historyAt], false);
    } else if (action == kActBrowserForward && b.historyAt + 1 < b.history.size()) {
        browserEnter(e, b.history[++b.historyAt], false);
    } else if (action >= kActBrowserCrumb && action < kActBrowserCrumb + kCrumbCap) {
        browserEnter(e, b.crumb[action - kActBrowserCrumb], true);
    }
}

void created(Engine& e, const fs::path& made) {
    if (made.empty()) {
        return;
    }
    browserShowFiles(e);
    browserShowTree(e);
    for (uint32_t i = 0; i < e.browser.files.size(); ++i) {
        if (e.browser.files[i].path == made) {
            browserOnFile(&e, i, false);
        }
    }
}

void onCreate(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    created(e, browserCreate(e.browser.cwd, static_cast<NewFile>(action - kActBrowserCreate)));
}

// Second header button: the native open dialog; browserSync copies the chosen files in when it closes.
void onImport(void* user, uint32_t) {
    auto& e = *static_cast<Engine*>(user);
    if (windowAskFile(e.host.view[e.slot].window, nullptr, 0, FileAsk::OpenMany)) {
        e.browser.importPending = true;
    }
}

void pollImport(Engine& e) {
    BrowserState& b = e.browser;
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
    created(e, last);
}

void onTune(void* user, uint32_t action) {
    auto& e = *static_cast<Engine*>(user);
    BrowserState& b = e.browser;
    if (action == kActBrowserView) {
        b.listView = !b.listView;
        guiHostSetText(e.gui, "tuneMenu::view", b.listView ? "View: List" : "View: Grid");
        browserRestyleTiles(e);
        return;
    }
    if (action == kActBrowserSort) {
        b.sort = static_cast<SortMode>((static_cast<uint32_t>(b.sort) + 1u) % 4u);
        guiHostSetText(e.gui, "tuneMenu::sort", kSortName[static_cast<uint32_t>(b.sort)]);
    }
    browserShowFiles(e);
}

void setTile(Engine& e, float px) {
    BrowserState& b = e.browser;
    b.tileF = std::clamp(px, guiHostToken(e.gui, "tile.min"), guiHostToken(e.gui, "tile.max"));
    const uint8_t rounded = static_cast<uint8_t>(std::lround(b.tileF));
    if (rounded != b.tilePx) {
        b.tilePx = rounded;
        browserRestyleTiles(e);
    }
}

void onTileSize(void* user, float value) {
    setTile(*static_cast<Engine*>(user), value);
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
        browserOpen(e, item);
    } else if (action == kActCtxReveal) {
        const std::string full = item.path.string();
        const char* one = full.c_str();
        desktopReveal(&one, 1);
    } else if (action == kActCtxDuplicate) {
        created(e, browserDuplicate(item.path));
    } else if (browserTrash(b.root, item.path)) {
        thumbsInvalidate(e, item.path.string().c_str());
        browserShowFiles(e);
        browserShowTree(e);
    }
}

} // namespace

const char* browserGridPath() {
    return kBrowserGrid;
}

const char* browserTreePath() {
    return kBrowserTree;
}

void browserSetRoot(Engine& e, const fs::path& dir) {
    BrowserState& b = e.browser;
    b.root = dir;
    b.expanded.clear();
    b.history.assign(1, dir);
    b.historyAt = 0;
    browserEnter(e, dir, false);
    browserShowTree(e);
}

void browserRefresh(Engine& e) {
    browserShowFiles(e);
    browserShowTree(e);
}

void browserSync(Engine& e) {
    thumbsSync(e);
    const float wheel = guiHostTakeZoom(e.gui);
    if (wheel != 0.0f && !e.browser.listView) {
        setTile(e, e.browser.tileF + wheel * guiHostToken(e.gui, "tile.step"));
        guiHostSetValue(e.gui, "tuneMenu::size", e.browser.tileF);
    }
    pollImport(e);
    browserPollMarquee(e);
    browserPointerSync(e);
}

bool browserBind(Engine& e) {
    std::error_code ec;
    BrowserState& b = e.browser;
    b.engine = &e;
    b.root = fs::current_path(ec);
    b.cwd = b.root;
    b.history.push_back(b.cwd);
    bool ok = thumbsInit(b.thumbs) && guiHostBindList(e.gui, kBrowserTree, browserOnTree, &e) && guiHostBindList(e.gui, kBrowserGrid, browserOnFile, &e) &&
              guiHostBindListContext(e.gui, kBrowserGrid, browserOnContext, &e);
    char node[96];
    path(node, sizeof(node), kBrowserBar, "search");
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
    ok = guiHostBindClick(e.gui, kActBrowserPage, onImport, &e) && ok;
    for (const uint32_t action : {kActBrowserSort, kActBrowserView}) {
        ok = guiHostBindClick(e.gui, action, onTune, &e) && ok;
    }
    guiHostSetListZoom(e.gui, kBrowserGrid, true);
    ok = browserDragBind(e) && ok;
    path(node, sizeof(node), kBrowserBar, "search");
    guiHostSetPlaceholder(e.gui, node, "Search...");
    guiHostSetValue(e.gui, "tuneMenu::size", e.browser.tileF);
    for (const uint32_t action : {kActCtxOpen, kActCtxReveal, kActCtxDuplicate, kActCtxDelete}) {
        ok = guiHostBindClick(e.gui, action, onContextAction, &e) && ok;
    }
    browserApplyTile(e);
    browserShowTree(e);
    browserShowFiles(e);
    return ok;
}

} // namespace burnhope

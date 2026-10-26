#include "Str.hpp"
#include "files/Browser.hpp"
#include "files/FileActions.hpp"
#include "files/FileBind.hpp"
#include "files/FileDomain.hpp"
#include "files/FileSession.hpp"
#include "files/FileTheme.hpp"
#include "ui/widget/AssetSlot.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/State.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

namespace burnhope {
namespace fs = std::filesystem;

void mountFiles(UiState& s) {
    UiFileBrowser* files = uiFilesFind(s);
    if (files == nullptr || files->fileFrame == kUiNone || fileBook(s).mounted != 0) {
        return;
    }
    Browser& b = fileBook(s);
    const char* layout = fileTheme(s).layout;
    if (uiImportBinFile(s, files->fileFrame, layout) == kUiNone) {
        spdlog::error("files: missing layout {}", layout);
        return;
    }
    b.mounted = 1;
    auto take = [&](const char* name) { return uiFindName(s, name); };
    b.plus = take("file.plus");
    b.back = take("file.back");
    b.fwd = take("file.fwd");
    b.tune = take("file.tune");
    b.pop = take("file.tunePanel");
    b.zoom = take("file.zoom");
    b.searchBtn = take("file.searchBtn");
    b.searchBox = take("file.search");
    b.filterBtn = take("file.filter");
    b.crumbs = take("file.crumbs");
    b.wTree = take("file.tree");
    b.status = take("file.status");
    files->fileView = take("file.grid");
    fileWire(s);
    if (const uint16_t split = take("file.split"); split != kUiNone) {
        uiSplitterSetWidth(s, split, b.look.tree);
    }
    fileRefresh(s);
}

FileModel* filesModel(UiState& s) {
    auto* session = s.world.try_get_mut<FileSession>();
    if (session == nullptr || session->ready == 0) {
        return nullptr;
    }
    return &session->browser;
}

void filesMount(UiState& s) { mountFiles(s); }

void filesClick(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    const UiAction* act = s.ent[id].try_get<UiAction>();
    if (act != nullptr && act->fn != nullptr) {
        act->fn(act->user, id);
    }
}

void filesShutdown(UiState& s) {
    if (auto* session = s.world.try_get_mut<FileSession>()) {
        const uint8_t folder = session->browser.folderSlot;
        const uint8_t file = session->browser.fileSlot;
        session->browser = {};
        session->browser.folderSlot = folder;
        session->browser.fileSlot = file;
        session->ready = 0;
    }
}

void filesShow(UiState& s, bool on) {
    fileBook(s);
    if (const UiFileBrowser* files = uiFilesFind(s); files != nullptr && files->fileFrame != kUiNone) {
        uiShow(s, files->fileFrame, on);
    }
    uiNotifyVisualDirty(s);
}

void filesGo(UiState& s, const char* path) {
    fileBook(s).go(path);
    fileRefresh(s);
}

void filesStyle(UiState& s, const FileLook& look) {
    Browser& b = fileBook(s);
    b.look = look;
    b.showHidden = look.showHidden;
    b.reload();
    fileRefresh(s);
}

void filesOnOpen(UiState& s, FileOpenFn fn, void* user) {
    Browser& b = fileBook(s);
    b.onOpen = fn;
    b.user = user;
}

void filesOfferPath(UiState& s, const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    if (uiAssetOfferPath(s, path)) {
        return;
    }
    const UiFileBrowser* files = uiFilesFind(s);
    if (files == nullptr || files->fileTarget == kUiNone) {
        return;
    }
    uiSetFieldText(s, files->fileTarget, path);
}

void filesDropOnScene(UiState& s, uint16_t view, void (*fn)(void* user, const char* path, float x, float y), void* user) {
    Browser& b = fileBook(s);
    b.sceneView = view;
    b.onImport = fn;
    b.user = user;
    if (view == kUiNone) {
        return;
    }
    uiDragAddHost(s, view);
    b.root[0] = '\0';
    b.cwd[0] = '\0';
    b.items.clear();
    b.tree.clear();
    b.hist.clear();
    b.histAt = 0;
    b.total = 0;
    fileRefresh(s);
}

void filesDropBadge(UiState& s, void (*fn)(void* user, char* dst, uint8_t cap)) {
    fileBook(s).onBadge = fn;
}

const char* filesDraggedOver(UiState& s, uint16_t paneId, float& outX, float& outY) {
    const UiDragDrop* drag = s.world.try_get<UiDragDrop>();
    if (drag == nullptr || drag->live == 0 || paneId >= s.count || !hitBox(uiNodeBox(s, paneId), drag->dropX, drag->dropY)) {
        return nullptr;
    }
    const Browser& b = fileBook(s);
    if (drag->srcIndex >= b.items.size()) {
        return nullptr;
    }
    outX = drag->dropX;
    outY = drag->dropY;
    return b.items[drag->srcIndex].name.c_str();
}

void filesRoot(UiState& s, const char* path) {
    Browser& b = fileBook(s);
    if (path == nullptr || path[0] == '\0') {
        b.root[0] = '\0';
        b.reload();
        fileRefresh(s);
        return;
    }
    std::error_code ec;
    const fs::path canon = fs::weakly_canonical(fs::path(path), ec);
    if (!ec) {
        copyCap(b.root, static_cast<int>(sizeof(b.root)), canon.string().c_str());
        b.hist.clear();
        b.histAt = 0;
        b.cwd[0] = '\0';
        b.go(b.root);
    }
    fileRefresh(s);
}

void uiClearDomain(UiState& s) {
    if (UiFileBrowser* files = uiFilesFind(s)) {
        files->photoView = kUiNone;
        files->fileView = kUiNone;
        files->fileFrame = kUiNone;
    }
}

bool filesWantsText(const UiState&) { return false; }

void filesAttach(UiState& s) {
    if (s.world.try_get<FileTheme>() == nullptr) {
        fileThemeInit(s, {});
    }
    uiClearAdd(s, uiClearDomain);
    s.plug.handle = fileActionsHandleKey;
    s.plug.draw = nullptr;
    s.plug.over = nullptr;
    s.plug.wantsText = filesWantsText;
    s.plug.shutdown = filesShutdown;
}

} // namespace burnhope

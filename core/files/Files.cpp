#include "Str.hpp"
#include "files/Browser.hpp"
#include "files/FileActions.hpp"
#include "files/FileBind.hpp"
#include "files/FileDomain.hpp"
#include "files/FileSession.hpp"
#include "files/FileTheme.hpp"
#include "ui/Domain.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Splitter.hpp"

#include <filesystem>
#include <spdlog/spdlog.h>

namespace burnhope {
namespace fs = std::filesystem;

void mountFiles(UiState& s) {
    if (uiFiles(s).fileFrame == kUiNone || fileBook(s).mounted != 0) {
        return;
    }
    Browser& b = fileBook(s);
    const char* layout = fileTheme(s).layout;
    if (uiImportBinFile(s, uiFiles(s).fileFrame, layout) == kUiNone) {
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
    uiFiles(s).fileView = take("file.grid");
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
    if (uiFiles(s).fileFrame != kUiNone) {
        uiShow(s, uiFiles(s).fileFrame, on);
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
    if (path == nullptr || path[0] == '\0' || uiFiles(s).fileTarget == kUiNone) {
        return;
    }
    uiSetFieldText(s, uiFiles(s).fileTarget, path);
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
        if (!b.underRoot(b.cwd)) {
            b.go(b.root);
        } else {
            b.reload();
        }
    }
    fileRefresh(s);
}

void uiClearDomain(UiState& s) {
    if (auto* files = s.world.try_get_mut<UiFileBrowser>()) {
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

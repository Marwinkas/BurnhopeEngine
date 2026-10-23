#include "files/FileSession.hpp"

#include "asset/ProjectDoc.hpp"
#include "files/FileDomain.hpp"
#include "files/Format.hpp"
#include "platform/Desktop.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/VirtualGrid.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;

Browser& fileBook(UiState& s) {
    FileSession& session = s.world.ensure<FileSession>();
    Browser& created = session.browser;
    if (session.ready == 0) {
        session.ready = 1;
        created.load();
        const FileTheme& theme = fileTheme(s);
        created.look.icon = theme.icon;
        created.look.tree = theme.tree;
        std::error_code ec;
        if (!fs::is_directory(fs::path(created.cwd), ec)) {
            copyCap(created.cwd, static_cast<int>(sizeof(created.cwd)), ".");
        }
        created.hist.clear();
        created.hist.emplace_back(created.cwd);
        created.histAt = 0;
        created.reload();
    }
    return created;
}

const char* fileLeaf(const char* path) {
    if (path == nullptr || path[0] == '\0') {
        return path;
    }
    const char* slash = std::strrchr(path, '/');
    return slash != nullptr && slash[1] != '\0' ? slash + 1 : path;
}

int fileFirstSelected(const Browser& b) {
    for (int i = 0; i < b.count(); ++i) {
        if (b.items[static_cast<size_t>(i)].selected != 0) {
            return i;
        }
    }
    return -1;
}

void fileShowFoot(UiState& s, const Browser& b) {
    if (b.status == kUiNone) {
        return;
    }
    char line[96]{};
    b.formatFoot(line, static_cast<int>(sizeof(line)));
    uiText(s, b.status, line);
}

void fileRefresh(UiState& s) {
    Browser& b = fileBook(s);
    b.rebuildTree();
    if (b.crumbs != kUiNone) {
        uiBreadcrumbsSet(s, b.crumbs, b.cwd, b.root[0] != '\0' ? b.root : nullptr);
    }
    if (b.wTree != kUiNone) {
        uiTreeViewSetCount(s, b.wTree, static_cast<uint32_t>(b.treeCount()));
    }
    if (uiFiles(s).fileView != kUiNone) {
        const bool rows = b.listView != 0 || b.groupOn != 0;
        const FileTheme& theme = fileTheme(s);
        uiVirtualGridSetView(
            s, uiFiles(s).fileView, static_cast<uint32_t>(b.count()), rows ? 1 : 0,
            rows ? theme.listItemW : b.look.icon, rows ? theme.listRowH : b.look.icon);
    }
    uiSetDisabled(s, b.back, !b.canBack());
    uiSetDisabled(s, b.fwd, !b.canForward());
    fileShowFoot(s, b);
    uiNotifyVisualDirty(s);
}

void fileOpenAt(UiState& s, int index) {
    Browser& b = fileBook(s);
    if (index < 0 || index >= b.count()) {
        return;
    }
    const std::string path = (fs::path(b.cwd) / b.items[static_cast<size_t>(index)].name).string();
    if (b.items[static_cast<size_t>(index)].isDir != 0) {
        if (uiFiles(s).fileView != kUiNone) {
            uiVirtualGridClearClick(s, uiFiles(s).fileView);
        }
        b.go(path.c_str());
        if (b.onOpen != nullptr) {
            b.onOpen(b.user, b.cwd, 1);
        }
    } else if (b.onOpen != nullptr) {
        const bool material = strExtIs(path.c_str(), ".bhmat");
        if (!material && !desktopOpen(path.c_str())) {
            b.setStatus("cannot open");
        }
        b.onOpen(b.user, path.c_str(), 0);
    }
    fileRefresh(s);
}

void fileBeginRename(UiState& s, int index) {
    Browser& b = fileBook(s);
    if (index < 0 || index >= b.count() || uiFiles(s).fileView == kUiNone) {
        return;
    }
    b.startRename(index);
    uiVirtualGridBeginEdit(s, uiFiles(s).fileView, static_cast<uint32_t>(index), b.items[static_cast<size_t>(index)].name.c_str());
    fileRefresh(s);
}

void fileMakeFolder(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = fileBook(s);
    char dst[sizeof(Browser::cwd)]{};
    uniquePath(dst, static_cast<int>(sizeof(dst)), b.cwd, "New Folder", "");
    std::error_code ec;
    fs::create_directory(fs::path(dst), ec);
    if (ec) {
        b.setStatus("create failed");
        return;
    }
    b.reload();
    const char* name = fileLeaf(dst);
    for (int i = 0; i < b.count(); ++i) {
        if (sameText(b.items[static_cast<size_t>(i)].name.c_str(), name)) {
            b.selectOnly(i);
            b.setStatus("new folder");
            fileBeginRename(s, i);
            return;
        }
    }
    b.setStatus("new folder");
    fileRefresh(s);
}

void fileMakeFile(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    fileBook(s).touchFile("New Document.txt");
    fileBook(s).reload();
    fileRefresh(s);
}

void fileMakeMaterial(void* user) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = fileBook(s);
    if (b.cwd[0] == '\0') {
        b.setStatus("no folder");
        return;
    }
    char dst[640]{};
    uniquePath(dst, static_cast<int>(sizeof(dst)), b.cwd, "Material", ".bhmat");
    MaterialInfo info{};
    info.base[0] = info.base[1] = info.base[2] = 1.0f;
    info.roughness = 0.5f;
    info.albedoGain = info.normalGain = info.ormGain = 1.0f;
    info.emissiveGain = 0.0f;
    if (!materialDocWrite(dst, info)) {
        b.setStatus("create failed");
        return;
    }
    b.reload();
    b.setStatus("new material");
    const char* name = fileLeaf(dst);
    for (int i = 0; i < b.count(); ++i) {
        if (sameText(b.items[static_cast<size_t>(i)].name.c_str(), name)) {
            b.selectOnly(i);
            fileBeginRename(s, i);
            return;
        }
    }
    fileRefresh(s);
}

} // namespace burnhope

#include "files/FileBind.hpp"

#include "Units.hpp"
#include "files/FileDomain.hpp"
#include "files/FileMenu.hpp"
#include "files/FileSession.hpp"
#include "files/FileTheme.hpp"
#include "files/Format.hpp"
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/DragDrop.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Thumb.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/VirtualGrid.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace burnhope {
namespace fs = std::filesystem;

namespace {

UiState& stateOf(void* user) { return *static_cast<UiState*>(user); }

std::string treePath(const Browser& b, uint32_t index) {
    if (index >= static_cast<uint32_t>(b.treeCount())) {
        return {};
    }
    const char* base = b.root[0] != '\0' ? b.root : b.cwd;
    if (b.tree[index].rel.empty()) {
        return base;
    }
    return (fs::path(base) / b.tree[index].rel).string();
}

void clearGridClick(UiState& s) {
    if (uiFiles(s).fileView != kUiNone) {
        uiVirtualGridClearClick(s, uiFiles(s).fileView);
    }
}

void onPlus(UiState& s, uint16_t id) {
    const UiBox& box = s.box[id];
    const UiMenuItem items[] = {
        {.label = "New Folder", .onClick = fileMakeFolder},
        {.label = "New Text Document", .onClick = fileMakeFile},
    };
    uiMenuOpen(s, box.x, box.y + box.h, items, &s, box.y);
}

void onBack(UiState& s) {
    clearGridClick(s);
    fileBook(s).goBack();
    fileRefresh(s);
}

void onFwd(UiState& s) {
    clearGridClick(s);
    fileBook(s).goForward();
    fileRefresh(s);
}

void onNav(UiState& s, const char* path) {
    fileBook(s).go(path);
    clearGridClick(s);
    fileRefresh(s);
}

void onTree(UiState& s, uint32_t index) {
    Browser& b = fileBook(s);
    const std::string path = treePath(b, index);
    if (path.empty()) {
        return;
    }
    b.go(path.c_str());
    fileRefresh(s);
}

uint8_t treeDepth(UiState& s, uint32_t index) {
    const Browser& b = fileBook(s);
    return index < static_cast<uint32_t>(b.treeCount()) ? b.tree[index].depth : 0;
}

uint8_t treeOpen(UiState& s, uint32_t index) {
    const Browser& b = fileBook(s);
    return index < static_cast<uint32_t>(b.treeCount()) && b.tree[index].open != 0 ? 1 : 0;
}

void bindNode(UiState& s, uint16_t row, uint32_t index) {
    Browser& b = fileBook(s);
    if (index >= static_cast<uint32_t>(b.treeCount())) {
        return;
    }
    const UiParts* parts = s.ent[row].try_get<UiParts>();
    if (parts == nullptr) {
        return;
    }
    uiThumb(s, parts->icon, b.folderSlot, UiIcon::Folder);
    const std::string& rel = b.tree[index].rel;
    const char* shown = rel.c_str();
    if (rel.empty()) {
        const char* base = b.root[0] != '\0' ? b.root : b.cwd;
        shown = fileLeaf(base);
    } else {
        shown = fileLeaf(rel.c_str());
    }
    uiText(s, parts->label, shown);
}

void onToggle(UiState& s, uint32_t index) {
    Browser& b = fileBook(s);
    if (index >= static_cast<uint32_t>(b.treeCount())) {
        return;
    }
    FileTreeNode& node = b.tree[index];
    node.open = node.open == 0 ? 1 : 0;
    fileRefresh(s);
}

void bindCell(UiState& s, uint16_t cell, uint32_t index) {
    Browser& b = fileBook(s);
    if (index >= static_cast<uint32_t>(b.count())) {
        return;
    }
    const bool header = b.items[index].isDir == 2;
    const UiParts* parts = s.ent[cell].try_get<UiParts>();
    const uint16_t label = parts != nullptr && parts->label != kUiNone ? parts->label : cell;
    uiText(s, label, b.items[index].name.c_str());
    if (parts != nullptr && parts->icon < s.count) {
        uiShow(s, parts->icon, !header);
        if (!header) {
            const bool dir = b.items[index].isDir != 0;
            uiThumb(s, parts->icon, dir ? b.folderSlot : b.fileSlot, dir ? UiIcon::Folder : UiIcon::Document);
        }
    }
}

uint8_t cellRole(UiState& s, uint32_t index) {
    const Browser& b = fileBook(s);
    if (index >= static_cast<uint32_t>(b.count())) {
        return kUiCellPlain;
    }
    if (b.items[index].isDir == 2) {
        return kUiCellHeader;
    }
    return b.items[index].selected != 0 ? kUiCellSelected : kUiCellPlain;
}

void onSelect(UiState& s, uint32_t index) {
    Browser& b = fileBook(s);
    const int at = static_cast<int>(index);
    if (at < 0 || at >= b.count()) {
        return;
    }
    if (b.items[index].isDir == 2) {
        const uint8_t bit = static_cast<uint8_t>(1u << (b.items[index].kind & 7));
        b.groupOpen = static_cast<uint8_t>(b.groupOpen ^ bit);
        b.reload();
        fileRefresh(s);
        return;
    }
    if (b.shift != 0) {
        b.selectRange(b.anchor, at);
    } else if (b.ctrl != 0) {
        b.toggleSelect(at);
    } else {
        b.selectOnly(at);
    }
    fileRefresh(s);
}

void onOpen(UiState& s, uint32_t index) {
    Browser& b = fileBook(s);
    if (b.ctrl != 0 || b.shift != 0) {
        return;
    }
    if (index < static_cast<uint32_t>(b.count()) && b.items[index].isDir == 2) {
        return;
    }
    fileOpenAt(s, static_cast<int>(index));
}

void onBandBegin(UiState& s) {
    Browser& b = fileBook(s);
    if (b.bandSnap == 0) {
        for (FileItem& item : b.items) {
            item.held = item.selected;
        }
        b.bandSnap = 1;
    }
    for (FileItem& item : b.items) {
        item.selected = b.ctrl != 0 ? item.held : 0;
    }
    clearGridClick(s);
}

void onBand(UiState& s, uint16_t child) {
    const UiVirtRow* row = child < s.count ? s.ent[child].try_get<UiVirtRow>() : nullptr;
    Browser& b = fileBook(s);
    if (row != nullptr && row->index < static_cast<uint32_t>(b.count()) && b.items[row->index].isDir != 2) {
        b.items[row->index].selected = 1;
        fileRefresh(s);
    }
}

void onSearch(UiState& s, const char* text) {
    Browser& b = fileBook(s);
    if (!sameText(b.search, text)) {
        copyCap(b.search, static_cast<int>(sizeof(b.search)), text);
        b.reload();
        fileRefresh(s);
    }
}

void onSearchBtn(UiState& s) {
    Browser& b = fileBook(s);
    b.searchOn = b.searchOn == 0 ? 1 : 0;
    const bool on = b.searchOn != 0;
    if (b.searchBox != kUiNone) {
        uiShow(s, b.searchBox, on);
        if (on) {
            uiFocus(s, b.searchBox);
        }
    }
    if (b.filterBtn != kUiNone) {
        uiShow(s, b.filterBtn, on);
    }
    uiNotifyLayoutDirty(s);
    uiNotifyVisualDirty(s);
}

void onHover(UiState& s, uint32_t index) {
    Browser& b = fileBook(s);
    if (b.status == kUiNone) {
        return;
    }
    if (index < static_cast<uint32_t>(b.count()) && b.items[index].isDir != 2) {
        char size[32]{};
        char line[sizeof(Browser::status)]{};
        formatFileSize(b.items[index].bytes, size, static_cast<int>(sizeof(size)));
        std::snprintf(line, sizeof(line), "%s  %s", b.items[index].name.c_str(), size);
        uiText(s, b.status, line);
        return;
    }
    char line[sizeof(Browser::status)]{};
    b.formatFoot(line, static_cast<int>(sizeof(line)));
    if (line[0] == '\0') {
        std::snprintf(line, sizeof(line), "%d", b.count());
    }
    uiText(s, b.status, line);
}

void onViewPick(UiState& s, uint8_t index) {
    fileBook(s).listView = index == 0 ? 0 : 1;
    fileRefresh(s);
}

void onSortPick(UiState& s, uint8_t index) {
    Browser& b = fileBook(s);
    b.sortKey = index;
    b.reload();
    fileRefresh(s);
}

void onGroupPick(UiState& s, uint8_t index) {
    Browser& b = fileBook(s);
    b.groupOn = index == 0 ? 0 : 1;
    b.reload();
    fileRefresh(s);
}

void onKindPick(UiState& s, uint8_t index) {
    Browser& b = fileBook(s);
    b.kind = static_cast<int>(index);
    b.reload();
    fileRefresh(s);
}

void applyIcon(UiState& s, float value) {
    Browser& b = fileBook(s);
    uiSliderSet(s, b.zoom, value);
    const FileTheme& theme = fileTheme(s);
    float shown = value;
    float minV = theme.iconMin;
    float maxV = theme.iconMax;
    if (!uiRange(s, b.zoom, shown, minV, maxV)) {
        if (value < minV) {
            value = minV;
        }
        if (value > maxV) {
            value = maxV;
        }
        shown = value;
    }
    if (b.look.icon == shown) {
        return;
    }
    b.look.icon = shown;
    fileRefresh(s);
}

void onTune(UiState& s, uint16_t id) {
    Browser& b = fileBook(s);
    if (b.pop == kUiNone || b.pop >= s.count) {
        return;
    }
    const bool open = b.popOn == 0;
    if (open && id < s.count) {
        const UiBox& box = s.box[id];
        const FileTheme& theme = fileTheme(s);
        uiPlace(s, b.pop, true, box.x, box.y + box.h + theme.popGap, theme.tuneW, theme.tuneH);
    } else {
        uiMenuClose(s);
    }
    uiShow(s, b.pop, open);
    b.popOn = open ? 1 : 0;
}

void onRenameCommit(UiState& s, const char* text) {
    fileBook(s).commitRename(text != nullptr ? text : "");
    fileRefresh(s);
}

void onRenameCancel(UiState& s) {
    fileBook(s).cancelRename();
    fileRefresh(s);
}

void onEmpty(UiState& s) {
    fileBook(s).clearSelection();
    fileRefresh(s);
}

uint8_t canDrag(UiState& s, uint16_t host, uint32_t index) {
    Browser& b = fileBook(s);
    if (host != uiFiles(s).fileView || index >= static_cast<uint32_t>(b.count())) {
        return 0;
    }
    return b.items[index].selected != 0 && b.items[index].isDir != 2 ? 1 : 0;
}

uint8_t canDrop(UiState& s, uint16_t host, uint32_t index) {
    Browser& b = fileBook(s);
    if (host == b.crumbs) {
        return 1;
    }
    if (host == b.wTree) {
        return index < static_cast<uint32_t>(b.treeCount()) ? 1 : 0;
    }
    if (host == uiFiles(s).fileView && index < static_cast<uint32_t>(b.count())) {
        return b.items[index].isDir == 1 ? 1 : 0;
    }
    return 0;
}

void onDrop(UiState& s, uint16_t dstHost, uint32_t dstIndex) {
    Browser& b = fileBook(s);
    std::string dest;
    if (dstHost == b.wTree) {
        dest = treePath(b, dstIndex);
        if (dest.empty()) {
            return;
        }
    } else if (dstHost == uiFiles(s).fileView && dstIndex < static_cast<uint32_t>(b.count()) && b.items[dstIndex].isDir == 1) {
        dest = (fs::path(b.cwd) / b.items[dstIndex].name).string();
    } else if (dstHost == b.crumbs) {
        dest = b.cwd;
    } else {
        return;
    }
    b.moveInto(dest.c_str());
    fileRefresh(s);
}

void dragBadge(UiState& s, char* dst, uint8_t cap) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    std::snprintf(dst, cap, "%d", fileBook(s).selectedCount());
}

} // namespace

void fileWire(UiState& s) {
    Browser& b = fileBook(s);
    uiBindName(s, "file.plus", [](void* user, uint16_t id) { onPlus(stateOf(user), id); }, &s);
    uiBindName(s, "file.back", [](void* user, uint16_t) { onBack(stateOf(user)); }, &s);
    uiBindName(s, "file.fwd", [](void* user, uint16_t) { onFwd(stateOf(user)); }, &s);
    uiBindName(s, "file.searchBtn", [](void* user, uint16_t) { onSearchBtn(stateOf(user)); }, &s);
    uiBindName(s, "file.tune", [](void* user, uint16_t id) { onTune(stateOf(user), id); }, &s);
    uiBindChoice(s, "file.view", b.listView == 0 ? uint8_t{0} : uint8_t{1}, [](void* user, uint8_t index) { onViewPick(stateOf(user), index); }, &s);
    uiBindChoice(s, "file.sort", b.sortKey < 4 ? b.sortKey : uint8_t{0}, [](void* user, uint8_t index) { onSortPick(stateOf(user), index); }, &s);
    uiBindChoice(s, "file.group", b.groupOn == 0 ? uint8_t{0} : uint8_t{1}, [](void* user, uint8_t index) { onGroupPick(stateOf(user), index); }, &s);
    uiBindChoice(s, "file.filter", static_cast<uint8_t>(b.kind < 5 ? b.kind : 0), [](void* user, uint8_t index) { onKindPick(stateOf(user), index); }, &s);
    uiBindSlider(s, "file.zoom", b.look.icon, [](void* user, float value) { applyIcon(stateOf(user), value); }, &s);
    uiBindTree(s, "file.tree", {
        .totalNodes = static_cast<uint32_t>(b.treeCount()),
        .onBindNode = [](void* user, uint16_t row, uint32_t index) { bindNode(stateOf(user), row, index); },
        .onToggleNode = [](void* user, uint32_t index) { onToggle(stateOf(user), index); },
        .onSelectNode = [](void* user, uint32_t index) { onTree(stateOf(user), index); },
        .onDepth = [](void* user, uint32_t index) { return treeDepth(stateOf(user), index); },
        .onOpen = [](void* user, uint32_t index) { return treeOpen(stateOf(user), index); },
        .onEmptyClick = [](void* user) { onEmpty(stateOf(user)); },
        .user = &s,
    });
    uiBindCrumbs(s, "file.crumbs", [](void* user, const char* path) { onNav(stateOf(user), path); }, &s);
    uiBindSearch(s, "file.search", [](void* user, const char* text) { onSearch(stateOf(user), text); }, &s);
    uiBindGrid(s, "file.grid", {
        .itemW = b.look.icon,
        .itemH = b.look.icon,
        .totalItems = static_cast<uint32_t>(b.count()),
        .onBindCell = [](void* user, uint16_t cell, uint32_t index) { bindCell(stateOf(user), cell, index); },
        .onSelect = [](void* user, uint32_t index, bool) { onSelect(stateOf(user), index); },
        .onActivate = [](void* user, uint32_t index) { onOpen(stateOf(user), index); },
        .onRole = [](void* user, uint32_t index) { return cellRole(stateOf(user), index); },
        .onHover = [](void* user, uint32_t index) { onHover(stateOf(user), index); },
        .onContext = fileOnContext,
        .onEmptyClick = [](void* user) { onEmpty(stateOf(user)); },
        .onZoom = [](void* user, float wheel) { applyIcon(stateOf(user), fileBook(stateOf(user)).look.icon + wheel * fileTheme(stateOf(user)).iconStep); },
        .onCommit = [](void* user, uint32_t, const char* text) { onRenameCommit(stateOf(user), text); },
        .onCancel = [](void* user, uint32_t) { onRenameCancel(stateOf(user)); },
        .user = &s,
    });
    if (uiFiles(s).fileView != kUiNone) {
        (void)uiRubberband(s, uiFiles(s).fileView, [](void* user, uint16_t child) { onBand(stateOf(user), child); }, &s, [](void* user) { onBandBegin(stateOf(user)); });
    }
    uiDragDropBind(s, {
        .host0 = uiFiles(s).fileView,
        .host1 = b.wTree,
        .host2 = b.crumbs,
        .canDrag = [](void* user, uint16_t host, uint32_t index) { return canDrag(stateOf(user), host, index); },
        .canDrop = [](void* user, uint16_t host, uint32_t index) { return canDrop(stateOf(user), host, index); },
        .onDrop = [](void* user, uint16_t, uint32_t, uint16_t dstHost, uint32_t dstIndex) { onDrop(stateOf(user), dstHost, dstIndex); },
        .badge = [](void* user, char* dst, uint8_t cap) { dragBadge(stateOf(user), dst, cap); },
        .user = &s,
    });
}

} // namespace burnhope

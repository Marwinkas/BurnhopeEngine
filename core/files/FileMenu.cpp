#include "files/FileMenu.hpp"

#include "files/FileSession.hpp"
#include "ui/widget/Menu.hpp"

namespace burnhope {

template <void (*Fn)(UiState&)>
void menuCall(void* user) {
    Fn(*static_cast<UiState*>(user));
}

void menuOpen(UiState& s) {
    const int at = fileFirstSelected(fileBook(s));
    if (at >= 0) {
        fileOpenAt(s, at);
    }
}

void menuRename(UiState& s) {
    const int at = fileFirstSelected(fileBook(s));
    if (at >= 0) {
        fileBeginRename(s, at);
    }
}

void menuDuplicate(UiState& s) {
    fileBook(s).duplicate();
    fileRefresh(s);
}

void menuCopy(UiState& s) { fileBook(s).copySelection(0); }
void menuCut(UiState& s) { fileBook(s).copySelection(1); }

void menuDelete(UiState& s) {
    fileBook(s).removeSelected();
    fileRefresh(s);
}

void menuReveal(UiState& s) { fileBook(s).reveal(); }

void menuPaste(UiState& s) {
    fileBook(s).paste();
    fileRefresh(s);
}

void menuSelectAll(UiState& s) {
    fileBook(s).selectAll();
    fileRefresh(s);
}

UiMenuItem menuItem(const char* label, void (*onClick)(void*)) {
    return {.label = label, .onClick = onClick};
}

UiMenuItem menuSep() {
    return {.label = "", .separator = true};
}

void fileMenuOpen(UiState& s, float x, float y, bool onItem) {
    if (onItem) {
        const UiMenuItem items[] = {
            menuItem("Open", menuCall<menuOpen>),
            menuItem("Rename (F2)", menuCall<menuRename>),
            menuItem("Duplicate (Ctrl+D)", menuCall<menuDuplicate>),
            menuSep(),
            menuItem("Copy (Ctrl+C)", menuCall<menuCopy>),
            menuItem("Cut (Ctrl+X)", menuCall<menuCut>),
            menuSep(),
            menuItem("Delete (Del)", menuCall<menuDelete>),
            menuItem("Reveal in OS", menuCall<menuReveal>),
        };
        uiMenuOpen(s, x, y, items, &s);
        return;
    }
    const UiMenuItem items[] = {
        menuItem("New Folder", fileMakeFolder),
        menuItem("New File", fileMakeFile),
        menuItem("Paste (Ctrl+V)", menuCall<menuPaste>),
        menuItem("Select All (Ctrl+A)", menuCall<menuSelectAll>),
    };
    uiMenuOpen(s, x, y, items, &s);
}

void fileOnContext(void* user, uint32_t index, float x, float y) {
    UiState& s = *static_cast<UiState*>(user);
    Browser& b = fileBook(s);
    const bool onItem = index < static_cast<uint32_t>(b.count()) && b.items[index].isDir != 2;
    if (onItem && b.items[index].selected == 0) {
        b.selectOnly(static_cast<int>(index));
        fileRefresh(s);
    }
    fileMenuOpen(s, x, y, onItem);
}

} // namespace burnhope

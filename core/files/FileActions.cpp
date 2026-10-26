#include "files/FileActions.hpp"

#include "files/FileDomain.hpp"
#include "files/FileSession.hpp"
#include "files/FileTheme.hpp"
#include "ui/widget/Popover.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/VirtualGrid.hpp"
#include "ui/State.hpp"

namespace burnhope {

bool fileActionsHandleKey(UiState& s, const InputFrame& in) {
    const FileSession* session = s.world.try_get<FileSession>();
    const UiFileBrowser* browser = uiFilesFind(static_cast<const UiState&>(s));
    if (session == nullptr || session->ready == 0 || browser == nullptr || browser->fileFrame == kUiNone
        || !nodeShown(s, browser->fileFrame)) {
        return false;
    }
    Browser& b = fileBook(s);
    if (b.mounted == 0) {
        return false;
    }
    b.ctrl = in.ctrl;
    b.shift = in.shift;
    const bool release = (in.pointer.released & kPointerLeft) != 0;
    if (release) {
        b.bandSnap = 0;
    }
    const FileTheme& theme = fileTheme(s);
    uiSyncPopover(s, b.pop, b.tune, theme.tuneW, theme.tuneH, b.popWant);
    const bool typing = (b.searchOn != 0 && s.focused == b.searchBox)
        || uiVirtualGridEditing(s, browser->fileView);
    if (typing) {
        return false;
    }
    if (in.f2 != 0) {
        const int at = fileFirstSelected(b);
        if (at >= 0) {
            fileBeginRename(s, at);
            return true;
        }
    }
    if (in.del != 0) {
        b.removeSelected();
        fileRefresh(s);
        return true;
    }
    if (in.copy != 0) {
        b.copySelection(0);
        return true;
    }
    if (in.cut != 0) {
        b.copySelection(1);
        return true;
    }
    if (in.pasteOn != 0) {
        b.paste();
        fileRefresh(s);
        return true;
    }
    if (in.selectAll != 0) {
        b.selectAll();
        fileRefresh(s);
        return true;
    }
    if (in.ctrl != 0 && in.textLen == 1 && (in.text[0] == 'd' || in.text[0] == 'D')) {
        b.duplicate();
        fileRefresh(s);
        return true;
    }
    if (in.enter != 0) {
        const int at = fileFirstSelected(b);
        if (at >= 0) {
            fileOpenAt(s, at);
            return true;
        }
    }
    return false;
}

} // namespace burnhope

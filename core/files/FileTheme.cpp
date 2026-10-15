#include "files/Browser.hpp"
#include "files/FileSession.hpp"
#include "files/FileTheme.hpp"

namespace burnhope {

void filesLoadIcons(Canvas& c) {
    if (c.state == nullptr) {
        return;
    }
    auto loadIcon = [&](uint8_t slot, const char* const* paths, int n) {
        for (int i = 0; i < n; ++i) {
            if (canvasLoadPhoto(c, slot, paths[i])) {
                return true;
            }
        }
        return false;
    };
    const FileTheme& theme = fileTheme(*c.state);
    const char* folderPaths[] = {theme.folderIcon, theme.folderIconAlt};
    const char* filePaths[] = {theme.fileIcon, theme.fileIconAlt};
    const uint8_t folder = filePhotoIndex(theme.folderSlot);
    const uint8_t file = filePhotoIndex(theme.fileSlot);
    const bool folderOk = folder != 255 && loadIcon(folder, folderPaths, 2);
    const bool fileOk = file != 255 && loadIcon(file, filePaths, 2);
    filesIconSlots(*c.state, folderOk ? folder : 255, fileOk ? file : 255);
}

void filesIconSlots(UiState& s, uint8_t folderSlot, uint8_t fileSlot) {
    FileSession& session = s.world.ensure<FileSession>();
    session.browser.folderSlot = folderSlot;
    session.browser.fileSlot = fileSlot;
    if (session.ready != 0 && session.browser.mounted != 0) {
        fileRefresh(s);
    }
}

} // namespace burnhope

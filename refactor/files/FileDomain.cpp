#include "files/FileDomain.hpp"
#include "ui/State.hpp"

namespace burnhope {

UiFileBrowser* uiFilesFind(UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (UiFileBrowser* files = s.ent[i].try_get_mut<UiFileBrowser>()) {
            return files;
        }
    }
    return nullptr;
}

const UiFileBrowser* uiFilesFind(const UiState& s) {
    for (uint16_t i = 0; i < s.count; ++i) {
        if (const UiFileBrowser* files = s.ent[i].try_get<UiFileBrowser>()) {
            return files;
        }
    }
    return nullptr;
}

UiFileBrowser& uiFiles(UiState& s) {
    if (UiFileBrowser* found = uiFilesFind(s)) {
        return *found;
    }
    s.ent[0].set<UiFileBrowser>({});
    return *s.ent[0].try_get_mut<UiFileBrowser>();
}

void uiFilesOwn(UiState& s, uint16_t frame) {
    if (frame >= s.count) {
        return;
    }
    UiFileBrowser copy{};
    uint16_t old = kUiNone;
    for (uint16_t i = 0; i < s.count; ++i) {
        if (const UiFileBrowser* files = s.ent[i].try_get<UiFileBrowser>()) {
            copy = *files;
            old = i;
            break;
        }
    }
    copy.fileFrame = frame;
    if (old != kUiNone && old != frame) {
        s.ent[old].remove<UiFileBrowser>();
    }
    s.ent[frame].set<UiFileBrowser>(copy);
}

const UiFileBrowser& uiFiles(const UiState& s) {
    return *uiFilesFind(s);
}

} // namespace burnhope

#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiFileBrowser {
    char fileDir[192]{};
    char fileName[16][40]{};
    uint8_t fileIsDir[16]{};
    uint8_t fileCount = 0;
    uint8_t filePick = 0xff;
    uint16_t fileRows[16]{};
    uint16_t filePanel = kUiNone;
    uint16_t fileHead = kUiNone;
    uint16_t fileTarget = kUiNone;
    uint16_t photoView = kUiNone;
    uint16_t fileView = kUiNone;
    uint16_t fileFrame = kUiNone;
    void* files = nullptr;
};

inline UiFileBrowser& uiFiles(UiState& s) {
    return s.world.ensure<UiFileBrowser>();
}
inline const UiFileBrowser& uiFiles(const UiState& s) {
    return *s.world.try_get<UiFileBrowser>();
}

} // namespace burnhope

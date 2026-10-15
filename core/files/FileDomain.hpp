#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiFileBrowser {
    uint16_t fileTarget = kUiNone;
    uint16_t photoView = kUiNone;
    uint16_t fileView = kUiNone;
    uint16_t fileFrame = kUiNone;
};

inline UiFileBrowser& uiFiles(UiState& s) {
    return s.world.ensure<UiFileBrowser>();
}
inline const UiFileBrowser& uiFiles(const UiState& s) {
    return *s.world.try_get<UiFileBrowser>();
}

} // namespace burnhope

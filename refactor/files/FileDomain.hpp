#pragma once

#include "ui/Types.hpp"

#include <cstdint>

namespace burnhope {

struct UiFileBrowser {
    uint16_t fileTarget = kUiNone;
    uint16_t photoView = kUiNone;
    uint16_t fileView = kUiNone;
    uint16_t fileFrame = kUiNone;
};

UiFileBrowser* uiFilesFind(UiState& s);
const UiFileBrowser* uiFilesFind(const UiState& s);
UiFileBrowser& uiFiles(UiState& s);
void uiFilesOwn(UiState& s, uint16_t frame);
const UiFileBrowser& uiFiles(const UiState& s);

} // namespace burnhope

#pragma once

#include "files/Model.hpp"
#include "ui/Canvas.hpp"

namespace burnhope {

// File manager is a layer above widgets. It plugs into the canvas and builds
// its screen from the same buttons, fields, slider and splitter.
void filesAttach(UiState& s);
void filesMount(UiState& s);
void filesClick(UiState& s, uint16_t id);
void filesShutdown(UiState& s);
void filesShow(UiState& s, bool on);
void filesGo(UiState& s, const char* path);
void filesStyle(UiState& s, const FileLook& look);
void filesOnOpen(UiState& s, FileOpenFn fn, void* user);
void filesRoot(UiState& s, const char* path);
void filesNewProject(UiState& s, const char* path);
void filesOpenProject(UiState& s, const char* path);
// Photo slots 6 and 7 from assets/icons/folder.png and file.png. Pass 255 when missing; the grid then draws a neutral outline.
void filesIconSlots(UiState& s, uint8_t folderSlot, uint8_t fileSlot);
void filesOfferPath(UiState& s, const char* path);
void filesLoadIcons(Canvas& c);

} // namespace burnhope

#pragma once

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

} // namespace burnhope

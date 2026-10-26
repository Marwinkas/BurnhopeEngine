#pragma once

#include "files/FileThumb.hpp"
#include "files/Model.hpp"
#include "ui/rhi/Canvas.hpp"

namespace burnhope {

// File manager is a layer above widgets. It plugs into the canvas and builds
// its screen from the same buttons, fields, slider and splitter.
FileModel* filesModel(UiState& s);
void filesAttach(UiState& s);
void filesMount(UiState& s);
void filesClick(UiState& s, uint16_t id);
void filesShutdown(UiState& s);
void filesShow(UiState& s, bool on);
void filesGo(UiState& s, const char* path);
void filesStyle(UiState& s, const FileLook& look);
void filesOnOpen(UiState& s, FileOpenFn fn, void* user);
void filesRoot(UiState& s, const char* path);
void filesDropOnScene(UiState& s, uint16_t view, void (*fn)(void* user, const char* path, float x, float y), void* user);
void filesDropBadge(UiState& s, void (*fn)(void* user, char* dst, uint8_t cap));
void filesNewProject(UiState& s, const char* path);
void filesOpenProject(UiState& s, const char* path);
// Local photo indices (heap slot minus 662). Pass 255 when the icon is missing.
void filesIconSlots(UiState& s, uint8_t folderSlot, uint8_t fileSlot);
// Material previews: `fn` bakes a .bhmat into cell `cell` of the 8 x 8 atlas that lives in photo slot `photoSlot`.
void filesThumbs(UiState& s, uint8_t photoSlot, FileThumbFn fn, void* user);
// Drops every baked preview and rebakes the visible ones (a material or one of its maps changed).
void filesThumbsInvalidate(UiState& s);
void filesOfferPath(UiState& s, const char* path);
// Offers a dropped path to the browser, then shows an image in photo slot 0 or imports a .json layout.
void filesOfferDrop(Canvas& canvas, const char* path);
void filesLoadIcons(Canvas& c);
// Name of the item being dragged out of the file list while the pointer is over `pane`, else nullptr.
// Writes the pointer position to outX/outY. The name stays valid until the listing reloads.
const char* filesDraggedOver(UiState& s, uint16_t paneId, float& outX, float& outY);

} // namespace burnhope

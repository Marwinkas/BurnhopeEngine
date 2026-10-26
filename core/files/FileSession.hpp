#pragma once

#include "files/FileTheme.hpp"
#include "files/Format.hpp"
#include "files/FileThumb.hpp"
#include "files/Model.hpp"

#include <cstdint>

namespace burnhope {

struct Browser : FileModel {
    FileLook look{};
    FileThumbs thumbs{};
    FileOpenFn onOpen = nullptr;
    void* user = nullptr;
    uint16_t plus = kUiNone;
    uint16_t back = kUiNone;
    uint16_t fwd = kUiNone;
    uint16_t tune = kUiNone;
    uint16_t importBtn = kUiNone;
    uint16_t searchBox = kUiNone;
    uint16_t filterBtn = kUiNone;
    uint16_t pop = kUiNone;
    uint16_t crumbs = kUiNone;
    uint16_t wTree = kUiNone;
    uint16_t status = kUiNone;
    uint16_t sceneView = kUiNone;
    uint16_t zoom = kUiNone;
    void (*onImport)(void* user, const char* path, float x, float y) = nullptr;
    void (*onBadge)(void* user, char* dst, uint8_t cap) = nullptr;
    bool (*onAskImport)(void* user) = nullptr;
    void* askUser = nullptr;
    uint8_t ctrl = 0;
    uint8_t shift = 0;
    uint8_t popWant = 0;
    uint8_t listView = 0;
    uint8_t bandSnap = 0;
    uint8_t folderSlot = 255;
    uint8_t fileSlot = 255;
    uint8_t classSlot[kFileIconClasses]{255, 255, 255};
    uint8_t mounted = 0;
};

struct FileSession {
    Browser browser{};
    uint8_t ready = 0;
};

Browser& fileBook(UiState& s);
void fileCrumbShow(UiState& s, uint16_t id, const char* path, const char* root);
bool fileCrumbPath(const char* path, const char* root, uint16_t index, char* out, uint32_t cap);
void fileRefresh(UiState& s);
void fileOpenAt(UiState& s, int index);
void fileBeginRename(UiState& s, int index);
void fileMakeFolder(void* user, uint16_t item);
void fileMakeFile(void* user, uint16_t item);
void fileMakeMaterial(void* user, uint16_t item);
// Copies the packed, NUL-separated `paths` into the current folder and reloads the listing. Returns the number copied.
int fileImportPaths(UiState& s, const char* paths, int count);
int fileFirstSelected(const Browser& b);
const char* fileLeaf(const char* path);

} // namespace burnhope

#pragma once

#include "ui/Primitive.hpp"
#include "ui/State.hpp"

#include <cstdint>
#include <type_traits>

namespace burnhope {

// Visual metrics, layout path, icon paths, and photo heap slots for the file browser.
// Heap slots sit in the UI photo range 662..669. The photo loader takes the local index.
struct alignas(16) FileTheme {
    const char* layout = "build-core/ui/files.uib";
    const char* folderIcon = "assets/icons/folder.png";
    const char* folderIconAlt = "../assets/icons/folder.png";
    const char* fileIcon = "assets/icons/file.png";
    const char* fileIconAlt = "../assets/icons/file.png";
    float listItemW = 480.0f;
    float popGap = 4.0f;
    float listRowH = 36.0f;
    float iconMin = 48.0f;
    float iconMax = 256.0f;
    float iconStep = 8.0f;
    float tuneW = 220.0f;
    float tuneH = 120.0f;
    float icon = 84.0f;
    float tree = 188.0f;
    uint32_t folderSlot = kUiDescPhoto;
    uint32_t fileSlot = kUiDescPhoto + 1;
    uint8_t pad[8]{};
};

static_assert(std::is_trivially_copyable_v<FileTheme>);
static_assert(sizeof(FileTheme) % 16 == 0);
static_assert(kUiDescPhoto == 662);
static_assert(kUiDescPhoto + 1 <= 669);

inline uint8_t filePhotoIndex(uint32_t heapSlot) {
    if (heapSlot < kUiDescPhoto || heapSlot >= kUiDescPhoto + kPhotoSlots) {
        return 255;
    }
    return static_cast<uint8_t>(heapSlot - kUiDescPhoto);
}

inline const FileTheme& fileTheme(const UiState& s) {
    if (const FileTheme* theme = s.world.try_get<FileTheme>()) {
        return *theme;
    }
    static constexpr FileTheme fallback{};
    return fallback;
}

inline void fileThemeInit(UiState& s, const FileTheme& theme) {
    s.world.set<FileTheme>(theme);
}

} // namespace burnhope

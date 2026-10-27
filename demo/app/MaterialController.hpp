#pragma once

#include <cstdint>
#include <memory>

#include "asset/MaterialBake.hpp"

namespace burnhope {

struct Engine;

struct MaterialField {
    Engine* engine = nullptr;
    uint32_t index = 0;
};

// The material editor: the open .bhmat document, its baked map atlas (photo slot 9) and the shaded preview (photo slot 8).
struct MaterialState {
    BhmatDoc doc{};
    char path[512]{};
    bool open = false;
    bool atlasDirty = false;
    bool previewDirty = false;
    uint8_t shape = 0; // MatShape
    float yaw = 0.0f;
    float pitch = 0.0f;
    MaterialField field[32]{};
    std::unique_ptr<uint8_t[]> atlas;
    std::unique_ptr<uint8_t[]> preview;
};

// Mounts assets/gui/material.json into the main row and binds its cards, sliders, pills and buttons.
[[nodiscard]] bool materialBind(Engine& e);
// Loads a .bhmat into the editor and shows its panel. False when the file cannot be read.
[[nodiscard]] bool materialOpen(Engine& e, const char* path);
// Re-bakes and uploads the atlas / preview when the document changed.
void materialSync(Engine& e);

} // namespace burnhope

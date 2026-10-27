#pragma once

#include <cstdint>
#include <vector>

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
    bool sceneDirty = false;      // the document changed and the target actor still shows the old one
    uint8_t target = 0xff;        // actor that shows the document live (kSceneActorNone = none)
    uint8_t shape = 0; // MatShape
    float yaw = 0.0f;
    float pitch = 0.0f;
    MaterialField field[32]{};
    std::vector<uint8_t> atlas;
    std::vector<uint8_t> preview;
};

// Mounts assets/gui/material.json into the main row and binds its cards, sliders, pills and buttons.
[[nodiscard]] bool materialBind(Engine& e);
// Shows `path` on `actor` (the document is applied to its material at once).
[[nodiscard]] bool materialAssign(Engine& e, const char* path, uint8_t actor);
// Loads a .bhmat into the editor and shows its panel. False when the file cannot be read.
[[nodiscard]] bool materialOpen(Engine& e, const char* path);
// Re-bakes and uploads the atlas / preview and pushes the document to the target actor when it changed.
void materialSync(Engine& e);

} // namespace burnhope

#pragma once

#include "gfx/Material.hpp"

#include <cstdint>

namespace burnhope {

constexpr uint32_t kEngineVersion = 1;

struct ProjectInfo {
    uint32_t version = kEngineVersion;
    char name[64]{};
    char about[256]{};
};

struct SceneObjectRec {
    char name[24]{};
    char mesh[260]{};
    char material[260]{};
    float pos[3]{};
    float euler[3]{};
    float scale[3]{1.0f, 1.0f, 1.0f};
    float base[3]{1.0f, 1.0f, 1.0f};
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissive = 0.0f;
    uint8_t kind = 0;
};

using MaterialInfo = BhmatDoc;

// Walks `root` (four levels deep, skipping cache and build folders) and calls `take` for every .bhmat file.
// `take` returns false to stop the walk.
using ProjectMaterialFn = bool (*)(void* user, const char* name, const char* path);
void projectListMaterials(const char* root, ProjectMaterialFn take, void* user);

// Copies every map the material names into the folder of `matPath`, then bakes the ORMH pack when the
// material owns it. `editedChannel` is the map the user just changed (2 = the pack itself, 255 = none).
// Returns true when the material's own ORMH file was rewritten.
[[nodiscard]] bool materialCommitMaps(BhmatDoc& doc, const char* matPath, uint8_t editedChannel);

[[nodiscard]] bool projectWrite(const char* dir, const ProjectInfo& info);
[[nodiscard]] bool projectRead(const char* path, ProjectInfo& info);
[[nodiscard]] bool sceneDocWrite(const char* path, const SceneObjectRec* recs, uint32_t count);
[[nodiscard]] bool sceneDocRead(const char* path, SceneObjectRec* recs, uint32_t cap, uint32_t& count);
[[nodiscard]] inline bool materialDocWrite(const char* path, const MaterialInfo& info) {
    return materialSaveBhmat(path, info);
}

[[nodiscard]] inline bool materialDocRead(const char* path, MaterialInfo& info) {
    return materialLoadBhmat(path, info);
}

} // namespace burnhope

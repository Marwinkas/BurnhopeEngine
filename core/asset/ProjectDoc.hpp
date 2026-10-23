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

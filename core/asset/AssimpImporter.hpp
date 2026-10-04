#pragma once

#include <cstdint>

namespace burnhope {

[[nodiscard]] bool sceneImportFbx(const char* fbxPath, const char* bhopPath);
[[nodiscard]] bool sceneImportFbxList(const char* const* fbxPaths, uint32_t count, const char* bhopPath);

} // namespace burnhope

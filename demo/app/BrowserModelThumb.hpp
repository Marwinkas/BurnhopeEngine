#pragma once

#include <cstdint>
#include <filesystem>

#include "asset/ModelThumb.hpp"

namespace burnhope {

// One background thread that bakes 3D model previews (mesh load + software raster, see asset/ModelThumb) so the frame loop never
// waits for a big .gltf / .fbx. Finished previews are also stored on disk: <project root>/cache/thumbs/<path hash>_<mtime>.bin.
struct ModelBaker;
struct ModelBakerDelete {
    void operator()(ModelBaker* baker) const;
};

[[nodiscard]] ModelBaker* modelBakerCreate();

enum class ModelThumbState : uint8_t { Pending, Ready };

// Preview of `path` (the model file) in `rgba` (kModelThumbBytes): Ready from the disk cache, or when a finished bake of this very key is
// waiting; otherwise the bake is started (one at a time) and the state is Pending - ask again on a later frame.
[[nodiscard]] ModelThumbState modelThumbFetch(ModelBaker& baker, const std::filesystem::path& root, const char* path, uint64_t key, int64_t mtime,
                                              uint8_t* rgba);

} // namespace burnhope

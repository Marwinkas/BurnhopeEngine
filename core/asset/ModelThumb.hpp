#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>

#include "asset/ModelMesh.hpp"

namespace burnhope {

constexpr uint32_t kModelThumbPx = 64;
constexpr uint32_t kModelThumbBytes = kModelThumbPx * kModelThumbPx * 4;

// Software preview of a model: centred on the origin and scaled uniformly into [-1, 1], seen from yaw 35 / pitch -25 degrees
// (orthographic, fitted to the cell), flat-shaded by one key sun plus a hemispherical ambient fill, 2x2 supersampled onto
// `plate` (RGB). `rgba` holds kModelThumbBytes. False when the mesh has no finite triangle (the cell is filled with the plate).
bool modelBakeThumb(const ModelMesh& mesh, const uint8_t plate[3], uint8_t* rgba);

struct JobPool;

// One background job (platform/Jobs) that bakes model previews so the frame loop never waits for a big .gltf / .fbx. Finished
// previews are also stored on disk: <project root>/cache/thumbs/<path hash>_<mtime>.bin.
struct ModelBaker;
struct ModelBakerDelete {
    void operator()(ModelBaker* baker) const;
};
using ModelBakerPtr = std::unique_ptr<ModelBaker, ModelBakerDelete>;

// `plate` (RGB) fills a preview of a model that cannot be loaded. The pool must outlive the baker.
[[nodiscard]] ModelBaker* assetModelBakerCreate(JobPool& pool, const uint8_t plate[3]);

enum class ModelThumbState : uint8_t { Pending, Ready };

// Preview of `path` (the model file) in `rgba` (kModelThumbBytes): Ready from the disk cache, or when a finished bake of this very key is
// waiting; otherwise the bake is started (one at a time) and the state is Pending - ask again on a later frame.
[[nodiscard]] ModelThumbState assetFetchModelThumb(ModelBaker& baker, const std::filesystem::path& root, const char* path, uint64_t key,
                                                   int64_t mtime, uint8_t* rgba);

} // namespace burnhope

#pragma once

#include <cstdint>

#include "asset/ModelMesh.hpp"

namespace burnhope {

constexpr uint32_t kModelThumbPx = 64;
constexpr uint32_t kModelThumbBytes = kModelThumbPx * kModelThumbPx * 4;

// Software preview of a model: centred on the origin and scaled uniformly into [-1, 1], seen from yaw 35 / pitch -25 degrees
// (orthographic, fitted to the cell), flat-shaded by one key sun plus a hemispherical ambient fill, 2x2 supersampled onto
// `plate` (RGB). `rgba` holds kModelThumbBytes. False when the mesh has no finite triangle (the cell is filled with the plate).
bool modelBakeThumb(const ModelMesh& mesh, const uint8_t plate[3], uint8_t* rgba);

} // namespace burnhope

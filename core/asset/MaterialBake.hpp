#pragma once

#include "gfx/Material.hpp"

#include <cstdint>

namespace burnhope {

// Material editor preview: a 4x3 atlas of 64 px cells (one per map) and a software-shaded sphere.
constexpr uint32_t kMatCell = 64;
constexpr uint32_t kMatAtlasW = 256;
constexpr uint32_t kMatAtlasH = 192;
constexpr uint32_t kMatSphereSide = 168;

// Cells: albedo, metal, rough, normal, height, ao, orm, emissive, alpha. A missing map is a flat swatch of
// the constant it stands for. Map paths resolve as given, then relative to the folder of `matPath`.
// `atlas` holds kMatAtlasW * kMatAtlasH RGBA bytes.
void materialBakeAtlas(const BhmatDoc& doc, const char* matPath, uint8_t* atlas);

// Shades a UV sphere from the atlas albedo cell, tinted by doc.base. `dst` holds kMatSphereSide^2 RGBA bytes.
void materialBakeSphere(const BhmatDoc& doc, const uint8_t* atlas, float yaw, float pitch, uint8_t* dst);

} // namespace burnhope

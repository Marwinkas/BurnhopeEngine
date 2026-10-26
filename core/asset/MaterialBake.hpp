#pragma once

#include "gfx/Material.hpp"

#include <cstdint>

namespace burnhope {

// Material editor preview: a 4x3 atlas of 64 px cells (one per map) and a software-shaded preview shape.
constexpr uint32_t kMatCell = 64;
constexpr uint32_t kMatAtlasW = 256;
constexpr uint32_t kMatAtlasH = 192;
constexpr uint32_t kMatSphereSide = 168;

// Cells: albedo, metal, rough, normal, height, ao, orm, emissive, alpha. A missing map is a flat swatch of
// the constant it stands for. Map paths resolve as given, then relative to the folder of `matPath`.
// `atlas` holds kMatAtlasW * kMatAtlasH RGBA bytes.
void materialBakeAtlas(const BhmatDoc& doc, const char* matPath, uint8_t* atlas);

enum class MatShape : uint8_t { Sphere, Cube, Plane };

// Path-free PBR preview of `shape` turned by yaw (about Y) then pitch (about X): a sun with GGX / Smith / Schlick
// specular plus a hemispherical dome, driven by the albedo, metal, roughness, normal, AO / ORM and emissive cells of
// the atlas and the scalars of `doc`. `zoom` scales the shape on screen (1 = default). `dst` holds kMatSphereSide^2 RGBA bytes.
void materialBakePreview(const BhmatDoc& doc, const uint8_t* atlas, MatShape shape, float yaw, float pitch, float zoom, uint8_t* dst);

} // namespace burnhope

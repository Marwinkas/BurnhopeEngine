#pragma once

#include "app/MaterialController.hpp"

namespace burnhope {

constexpr uint32_t kMatScalarCount = 7;
constexpr uint32_t kMatSlotCount = 9; // albedo, normal, metal, height, alpha, rough, ao, emission, orm

// Shared by MaterialController.cpp (panel, clicks, sync), MaterialFields.cpp (sliders, hex, the document <-> GUI mirror) and
// MaterialPick.cpp (texture slots, texture and colour pickers).

// The document changed: re-shade the preview and push it to the target actor.
void materialTouch(MaterialState& m);
// Same, and the map atlas is baked again (a map path, the albedo or the emission colour changed).
void materialTouchAtlas(MaterialState& m);
// Mirrors the whole document into the panel.
void materialPushDoc(MaterialState& m);
void materialPushVec(MaterialState& m);
void materialPushFlip(MaterialState& m);
void materialPushColors(MaterialState& m);
// Shows scalar `index` (kScalars order) on its slider and on its ORMH twin.
void materialPushScalar(MaterialState& m, uint32_t index);
// Type / Maps dropdown captions and the Double Sided / Repeat / Triplanar toggles.
void materialPushModes(MaterialState& m);
// Slot thumbnails ("None" badge for an empty slot) and the card visibility of the Classic / ORMH workflow.
void materialPushSlots(MaterialState& m);
// Puts scalar `index` back to its default value.
void materialResetScalar(MaterialState& m, uint32_t index);
// Sets the albedo (0) or emission (1) colour from the picker / reset button.
void materialSetColor(MaterialState& m, uint32_t which, const float rgb[3]);
// Binds every slider / hex field and the orbit pad of the mounted panel by its "bind" key.
[[nodiscard]] bool materialBindFields(MaterialState& m);
// Gives the visible rows of an open texture picker their thumbnails (at most a few bakes per call).
void materialPickSync(MaterialState& m);
// Binds the slot buttons, the swatches and the texture picker list.
[[nodiscard]] bool materialBindPickers(MaterialState& m);

} // namespace burnhope

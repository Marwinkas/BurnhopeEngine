#pragma once

#include "app/MaterialController.hpp"

namespace burnhope {

// Shared by MaterialController.cpp (panel, clicks, sync) and MaterialFields.cpp (sliders, paths, hex, the document <-> GUI mirror).
void materialTouch(MaterialState& m);
// Mirrors the whole document into the panel.
void materialPushDoc(MaterialState& m);
void materialPushVec(MaterialState& m);
void materialPushFlip(MaterialState& m);
// Binds every slider / path / hex field and the orbit pad of the mounted panel by its "bind" key.
[[nodiscard]] bool materialBindFields(MaterialState& m);

} // namespace burnhope

#pragma once

#include "app/MaterialController.hpp"

namespace burnhope {

// Shared by MaterialController.cpp (panel, clicks, sync) and MaterialFields.cpp (sliders, paths, hex, the document <-> GUI mirror).
void materialTouch(MaterialState& m);
// Mirrors the whole document into the panel.
void materialPushDoc(Engine& e);
void materialPushVec(Engine& e);
void materialPushFlip(Engine& e);
// Binds every slider / path / hex field and the orbit pad of the mounted panel by its "bind" key.
[[nodiscard]] bool materialBindFields(Engine& e);

} // namespace burnhope

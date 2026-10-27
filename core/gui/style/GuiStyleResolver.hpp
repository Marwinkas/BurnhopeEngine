#pragma once

#include <cstdint>

#include "gui/data/GuiComponents.hpp"
#include "gui/style/GuiStyleSheet.hpp"

namespace burnhope::gui {

// Applies a style class to an entity. Sets UiClass, UiPaint (token, borderToken, radius, borderW; tint alpha when the
// class has "alpha"; other UiPaint fields are kept), UiText size/align when the class sets them (UiText is added if
// missing), and every set layout field on the entity's Yoga node when it has a UiFlex. Fields the class leaves unset are
// not touched, so applying the same class again is a no-op and a class applied before explicit overrides never wipes them.
// Returns false (BH_ASSERT in debug) for an invalid class id or a dead entity.
bool guiStyleApply(const GuiStyleSheet& sheet, flecs::entity e, uint32_t classId);

} // namespace burnhope::gui

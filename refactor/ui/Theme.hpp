#pragma once

#include "ui/Style.hpp"
#include "ui/Types.hpp"

#include <string_view>

namespace burnhope {

constexpr uint8_t kUiThemeCap = 8;

// Replaces the named palettes with the ones in `json`:
//   {"themes": {"ink": {"window": [0.07, 0.07, 0.08], "accent0": [0.93, 0.45, 0.12], "radius": 3, ...}}}
// A theme starts from UiLook{} and overrides only what it lists. Color keys (three numbers each): window title side
// content button field menu close text muted item preview status slider check chrome accent0 accent1 accent2.
// Number keys: radius radiusSm motion hover. Glyph metrics, titleBar and resizeBorder belong to the font and the
// window, not the theme. Returns false (palettes untouched) for a document that does not parse, repeats a theme,
// lists an unknown key or has more than kUiThemeCap themes.
bool uiThemeLoad(UiState& s, std::string_view json);
bool uiThemeLoadAsset(UiState& s, const char* asset);

// The palette called `name`; false (out untouched) when no theme of that name is loaded.
[[nodiscard]] bool uiThemeLook(const UiState& s, const char* name, UiLook& out);

} // namespace burnhope

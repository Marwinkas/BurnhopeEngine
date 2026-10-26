#pragma once

#include "ui/Style.hpp"
#include "ui/Types.hpp"

#include <cstdint>
#include <string_view>

namespace burnhope {

// Numeric properties a style class can set. Every widget that consumes a class passes its own fallback,
// so a missing sheet or a missing key leaves the widget at its built-in size.
enum class UiStyleKey : uint8_t {
    Height,
    Inner,
    Width,
    MaxW,
    MinW,
    Pad,
    PadL,
    PadR,
    Gap,
    Border,
    Radius,
    Icon,
    Alpha,
    Trail,
    Count,
};

constexpr uint8_t kUiStyleCap = 24;

struct UiStyleClass {
    char name[24]{};
    float value[static_cast<size_t>(UiStyleKey::Count)]{};
    uint16_t has = 0;
    UiToken bg = UiToken::Custom;
    UiToken edge = UiToken::Custom;
};

struct UiStyleSheet {
    UiStyleClass cls[kUiStyleCap]{};
    uint8_t count = 0;
};

// Replaces the sheet with the classes in `json` ({"classes": {"name": {"h": 26, "border": 1, ...}}}) and applies
// the "window_panel" class to the popup look. Returns false (sheet untouched) when the text does not parse.
bool uiStyleLoad(UiState& s, std::string_view json);
bool uiStyleLoadAsset(UiState& s, const char* asset);

[[nodiscard]] const UiStyleClass* uiStyleFind(const UiState& s, const char* name);
[[nodiscard]] bool uiStyleHas(const UiStyleClass* cls, UiStyleKey key);
// The class value, or `fallback` when the class or the key is absent.
[[nodiscard]] float uiStyle(const UiState& s, const char* cls, UiStyleKey key, float fallback);

} // namespace burnhope

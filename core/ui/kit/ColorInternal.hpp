#pragma once

#include "ui/Primitive.hpp"
#include "ui/Style.hpp"
#include "ui/widget/ColorPicker.hpp"

namespace burnhope {

constexpr UiRamp kRampColorWheel = UiRamp::HueWheel;
constexpr UiRamp kRampColorValue = UiRamp::ValueRamp;
constexpr UiRamp kRampColorSwatch = UiRamp::Gradient;

// Shared between Color.cpp (wheel, sliders, fields, HSV/RGB sync) and ColorPopup.cpp (popup lifecycle,
// color book, eyedropper). Not part of the widget API.
UiPaint colorRampPart(const UiLook& look, UiToken token, UiRamp ramp, float radius = 0.0f);
void colorOnClose(void* user, uint16_t id);
void colorOnDrop(void* user, uint16_t id);
void colorOnMenu(void* user, uint16_t id);
void colorOnWindow(void* user, uint16_t id);
UiColorPickerComponent* pickerFor(UiState& s, uint16_t id);
void publishColor(UiState& s, UiColorPickerComponent& picker);
const UiColorPickerComponent* pickerFor(const UiState& s, uint16_t id);
UiColorPickerComponent* hostPicker(UiState& s);
UiColorPickerComponent& colorBind(UiState& s, uint16_t panel);
void applyColor(UiState& s, UiColorPickerComponent& picker, bool fromHsv, bool publish = true);
void takeBook(UiColorPickerComponent& picker);
void samplePixel(const UiState& s, float x, float y, float& r, float& g, float& b, float& a);
void refreshColorChrome(UiState& s, UiColorPickerComponent& picker);
void colorHead(UiState& s, uint16_t parent, const char* label, const char* cls = nullptr);
void placeColorMenu(UiState& s, UiColorPickerComponent& picker);
void fillColorBody(UiState& s, uint16_t parent);

} // namespace burnhope

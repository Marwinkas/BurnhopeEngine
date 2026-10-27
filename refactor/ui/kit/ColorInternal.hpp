#pragma once

#include "rhi/UiPrimitive.hpp"
#include "ui/Style.hpp"
#include "ui/widget/ColorPicker.hpp"

namespace burnhope {

struct UiField;

constexpr UiRamp kRampColorWheel = UiRamp::HueWheel;
constexpr UiRamp kRampColorValue = UiRamp::ValueRamp;
constexpr UiRamp kRampColorSwatch = UiRamp::Gradient;

// Shared by the picker's files. ColorRamps.cpp: ramp paint, range and hex text, HSV/RGB sync, the wheel drag.
// ColorHex.cpp: field and slider callbacks and the grab. ColorPicker.cpp: lookup, binding and the body build.
// ColorQuery.cpp: the public lookups. ColorPopup.cpp: popup lifecycle, color book, eyedropper. Not part of the widget API.
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
void setRange(UiState& s, uint16_t id, float value);
void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a);
void paintColor(UiState& s);
void syncColor(UiState& s, UiColorPickerComponent& picker);
void dragWheel(UiState& s, UiColorPickerComponent& picker, uint16_t id, float x, float y);
bool readHex(const UiField& field, float& r, float& g, float& b, float& a);
bool readInt(const UiField& field, int& out);
void colorOnValue(void* user, uint16_t id, float val);
void slideChannel(void* user, int channel, float value);
void slideR(void* user, float value);
void slideG(void* user, float value);
void slideB(void* user, float value);
void slideA(void* user, float value);
void slideH(void* user, float value);
void slideS(void* user, float value);
void slideV(void* user, float value);
void colorOnDrag(void* user, uint16_t id, float x, float y);
void colorOnEdit(void* user, uint16_t id);
bool colorGrab(void* user, uint16_t hit, float x, float y);

} // namespace burnhope

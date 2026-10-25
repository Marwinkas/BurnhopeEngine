#pragma once

#include "ui/Types.hpp"
#include "ui/Primitive.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

void uiImageBin(UiState& s);

uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h);
void uiSetIcon(UiState& s, uint16_t id, uint8_t slot);
void photoClamp(UiImage& image);
void uiArmPhoto(UiState& s, uint16_t id);
bool photoMinimap(const UiBox& box, float x, float y, float& u, float& v);
void photoZoom(UiState& s, uint16_t id, float wheel, float x, float y);
void photoPan(UiState& s, uint16_t id, float x, float y);

} // namespace burnhope

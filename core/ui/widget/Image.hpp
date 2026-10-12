#pragma once

#include "ui/State.hpp"

namespace burnhope {

uint16_t uiIcon(UiState& s, uint16_t parent, uint8_t slot, float w, float h);
void uiSetIcon(UiState& s, uint16_t id, uint8_t slot);
void photoClamp(UiImage& image);
void uiArmPhoto(UiState& s, uint16_t id);
bool photoMinimap(const UiBox& box, float x, float y, float& u, float& v);
void photoZoom(UiState& s, uint16_t id, float wheel, float x, float y);
void photoPan(UiState& s, uint16_t id, float x, float y);
void tickPhoto(UiState& s);
void uiApplyPhotoMeta(UiState& s, uint16_t id, uint8_t frames, const uint16_t* delays, uint32_t delayCount);

} // namespace burnhope

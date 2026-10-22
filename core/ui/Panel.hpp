#pragma once

#include "ui/Flex.hpp"
#include "ui/Geometry.hpp"
#include "ui/Style.hpp"

#include <cstdint>

namespace burnhope {

struct UiState;
struct UiPrimitive;

// Панель — заливка одного прямоугольника. Не знает кнопку, текст и курсор.
uint16_t uiPanel(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
uint16_t uiSpacer(UiState& s, uint16_t parent, const UiFlex& flex);

uint32_t uiEmitPanel(
    const UiState& s,
    uint16_t id,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox* clip,
    float r,
    float g,
    float b,
    float a,
    float glyphA = 1.0f);

} // namespace burnhope

#pragma once

#include "ui/Flex.hpp"
#include "ui/Geometry.hpp"
#include "ui/Icon.hpp"
#include "ui/Style.hpp"

#include <cstdint>

namespace burnhope {

struct UiState;
struct UiPrimitive;

// Панель — заливка одного прямоугольника. Не знает кнопку, текст и курсор.
uint16_t uiPanel(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
uint16_t uiSpacer(UiState& s, uint16_t parent, const UiFlex& flex);
// Re-skins one node with a theme token; the node keeps the rest of its paint.
void uiSetToken(UiState& s, uint16_t id, UiToken token);

// Node facade: callers never reach into the flex or paint components. A negative number leaves that value as it is.
// uiSetSize: 0 is auto, a positive number is pixels.
void uiSetSize(UiState& s, uint16_t id, float w, float h);
void uiSetWidthPercent(UiState& s, uint16_t id, float percent);
void uiSetPad(UiState& s, uint16_t id, float pad);
void uiSetGrow(UiState& s, uint16_t id, float grow, float shrink, float minW = -1.0f);
void uiSetGap(UiState& s, uint16_t id, float gap);
// Adds `delta` to the pixel width, never going below `minW`.
void uiAddWidth(UiState& s, uint16_t id, float delta, float minW);
[[nodiscard]] float uiStyleWidth(const UiState& s, uint16_t id, float fallback);
void uiSetIcon(UiState& s, uint16_t id, UiIcon icon);
void uiSetAlpha(UiState& s, uint16_t id, float alpha);

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

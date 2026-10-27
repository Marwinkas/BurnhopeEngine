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
// A blank node that takes the free room of its row or column; `cls` ("spacer_flex", "spacer") sets its thickness.
uint16_t uiSpacer(UiState& s, uint16_t parent, const char* cls = "spacer_flex");
// Re-skins one node with a theme token; the node keeps the rest of its paint.
void uiSetToken(UiState& s, uint16_t id, UiToken token);
// The node that holds the pointer capture (kUiNone when none), and the active theme.
[[nodiscard]] uint16_t uiCaptured(const UiState& s);
// The node under the pointer (kUiNone when none).
[[nodiscard]] uint16_t uiHovered(const UiState& s);
[[nodiscard]] const UiLook& uiLook(const UiState& s);

// Node facade: callers never reach into the flex or paint components. A negative number leaves that value as it is.
// uiSetSize: 0 is auto, a positive number is pixels.
void uiSetSize(UiState& s, uint16_t id, float w, float h);
// Rounds only one end of the node with the Radius of style class `cls` (see UiCap).
void uiSetCap(UiState& s, uint16_t id, UiCap cap, const char* cls);
void uiSetWidthPercent(UiState& s, uint16_t id, float percent);
void uiSetPad(UiState& s, uint16_t id, float pad);
void uiSetGrow(UiState& s, uint16_t id, float grow, float shrink, float minW = -1.0f);
void uiSetGap(UiState& s, uint16_t id, float gap);
// Per-instance placement of a spawned blueprint, as a layout file states it. A zero size, minimum or position leaves the
// blueprint's value; `shrink` below zero does too. `box` also applies direction, gap and padding (a panel, not a placed part).
struct UiLayoutSpec {
    float w = 0.0f;
    float h = 0.0f;
    float wp = 0.0f;
    float hp = 0.0f;
    float grow = 0.0f;
    float shrink = -1.0f;
    float gap = 0.0f;
    float pad = 0.0f;
    float padL = 0.0f;
    float padR = 0.0f;
    float padT = 0.0f;
    float padB = 0.0f;
    float minW = 0.0f;
    float minH = 0.0f;
    float posX = 0.0f;
    float posY = 0.0f;
    bool box = false;
    bool row = false;
    bool wrap = false;
    bool absolute = false;
    bool alignCenter = false;
    bool selfCenter = false;
    bool scroll = false;
};
void uiApplyLayout(UiState& s, uint16_t id, const UiLayoutSpec& spec);
// Row layout a button asks for: centred content with the "btn_row" gap.
void uiSetRowCentered(UiState& s, uint16_t id, float gap);
// Adds `delta` to the pixel width, never going below `minW`.
void uiAddWidth(UiState& s, uint16_t id, float delta, float minW);
[[nodiscard]] float uiStyleWidth(const UiState& s, uint16_t id, float fallback);
void uiSetIcon(UiState& s, uint16_t id, UiIcon icon);
// Switches a node between its class's idle and active look: the bg / activeBg tokens, or for a plate-less tool button
// the glyph ink. A node whose class has no activeBg only changes its glyph ink.
void uiSetActive(UiState& s, uint16_t id, bool on);

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

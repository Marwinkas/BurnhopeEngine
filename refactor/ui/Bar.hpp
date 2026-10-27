#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

// Screen rect a widget fills before the frame emits. on == 0 draws nothing.
struct UiBar {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float radius = 0.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float spread = 0.0f;
    uint32_t flags = 0;
    uint8_t on = 0;
};

// Extra rects and one icon on the node that owns them. A place hook fills `bar`
// from the frame scratch. `n` is the count that hook wrote. There is no fixed slot cap.
struct UiNodeBars {
    const UiBar* bar = nullptr;
    uint16_t n = 0;
    uint8_t icon = 0;
    float iconX = 0.0f;
    float iconY = 0.0f;
    float iconS = 0.0f;
    float iconR = 1.0f;
    float iconG = 1.0f;
    float iconB = 1.0f;
    float iconA = 1.0f;
};

// Glyph run the generic text pass skips. The batch emits it from this record.
struct UiTextInk {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float cx = 0.0f;
    float cy = 0.0f;
    float cw = 0.0f;
    float ch = 0.0f;
    uint8_t clip = 0;
    uint8_t on = 0;
};

// Shader mode and flags for a ramp the panel emits. The enum on the paint is not the shader mode.
struct UiRampDraw {
    uint32_t mode = 0;
    uint32_t flags = 0;
};

static_assert(std::is_trivially_copyable_v<UiBar>);
static_assert(sizeof(UiBar) % alignof(UiBar) == 0);
static_assert(std::is_trivially_copyable_v<UiNodeBars>);
static_assert(std::is_trivially_copyable_v<UiTextInk>);
static_assert(std::is_trivially_copyable_v<UiRampDraw>);

// Bars that are not owned by a node. A place hook fills `bar` from the frame scratch.
struct UiStamps {
    const UiBar* bar = nullptr;
    uint16_t n = 0;
};

static_assert(std::is_trivially_copyable_v<UiStamps>);

} // namespace burnhope

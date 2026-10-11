#pragma once

#include "ui/Model.hpp"

namespace burnhope {

struct UiState;
struct UiPrimitive;

// Shared color between a canvas and a detached picker window.
struct ColorBook {
    float r = 0.36f;
    float g = 0.55f;
    float b = 0.92f;
    float a = 1.0f;
    float h = 0.62f;
    float s = 0.61f;
    float v = 0.92f;
    uint8_t useAlpha = 1;
    uint8_t mode = 0;
    uint8_t present = 0;
    uint8_t drop = 0;
    uint32_t gen = 0;
};

void uiSetColorBook(UiState& s, ColorBook* book);
void uiBuildColor(UiState& s);
bool colorCustomDraw(UiState& s, uint16_t id, UiPrimitive* dst, uint32_t& n, uint32_t cap);

} // namespace burnhope

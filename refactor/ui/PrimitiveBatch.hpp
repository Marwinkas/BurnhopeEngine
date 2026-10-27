#pragma once

#include "ui/Geometry.hpp"
#include "rhi/UiPrimitive.hpp"
#include "ui/Style.hpp"

namespace burnhope {

struct UiState;
struct UiNodeBars;
struct UiBar;

void uiNodeBarPush(UiState& s, UiNodeBars& bars, const UiBar& bar);
// The one way a place hook adds a solid rect to `bars`; rects under half a pixel in either side are dropped.
void uiBarPush(UiState& s, UiNodeBars& bars, float x, float y, float w, float h, float r, float g, float b, float a, float radius, uint32_t flags = 0, float spread = 0.0f);
void uiBarScratchReset(UiState& s);
void uiBarScratchShutdown(UiState& s);

struct UiGpuHeader {
    float extentX;
    float extentY;
    uint32_t count;
    uint32_t pad;
};

uint32_t emitRect(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    float a,
    float radius,
    const UiBox* clip);
// One square atlas icon `icon` (a UiIcon value) of side `side`, tinted r,g,b,a and turned by `angle` degrees.
uint32_t emitIcon(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float side,
    float r,
    float g,
    float b,
    float a,
    uint32_t icon,
    float angle,
    const UiBox* clip);
uint32_t emitRamp(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    uint32_t mode,
    uint32_t kind);
uint32_t emitMark(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    float x,
    float y,
    float w,
    float h,
    float r,
    float g,
    float b,
    float a,
    float radius,
    float spread,
    uint32_t flag,
    const UiBox* clip);
uint32_t uiEmitSprite(
    const UiPaintStyle& style,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox& box,
    const UiImage& image,
    const UiBox* clip,
    float blur,
    float tintR,
    float tintG,
    float tintB,
    float tintA);
[[nodiscard]] bool uiClipBox(const UiState& s, uint16_t id, UiBox& out);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);

} // namespace burnhope

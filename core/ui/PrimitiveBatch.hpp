#pragma once

#include "ui/Model.hpp"

namespace burnhope {

struct UiState;
struct TextRun;

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
uint32_t emitPhoto(
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox& box,
    const UiImage& image,
    const UiBox* clip,
    float blur,
    float tintR = 1.0f,
    float tintG = 1.0f,
    float tintB = 1.0f);
[[nodiscard]] bool uiClipBox(const UiState& s, uint16_t id, UiBox& out);
[[nodiscard]] uint32_t uiEmit(UiState& s, UiPrimitive* dst, uint32_t cap);
[[nodiscard]] uint32_t uiEmitText(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const char* text,
    uint8_t len,
    const UiBox& box,
    float r,
    float g,
    float b,
    const UiBox* clip,
    bool center);
[[nodiscard]] uint32_t uiEmitRun(
    UiState& s,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const TextRun& run,
    float r,
    float g,
    float b,
    float a,
    const UiBox* clip);

} // namespace burnhope

#pragma once

#include "ui/Geometry.hpp"
#include "rhi/UiPrimitive.hpp"
#include "ui/Style.hpp"
#include "ui/Typography.hpp"

#include <cstdint>

namespace burnhope {

struct UiState;

struct UiWrapCut {
    int end = 0;
    int next = 0;
    float width = 0.0f;
};

inline UiWrapCut uiWrapCut(const float* ps, const uint8_t* kind, int n, int at, float limit, int breakAll) noexcept {
    UiWrapCut cut{};
    if (ps == nullptr || kind == nullptr || at >= n) {
        return cut;
    }
    if (kind[at] == 1) {
        cut.end = at;
        cut.next = at + 1;
        return cut;
    }
    int j = at;
    int lastSpace = -1;
    while (j < n && kind[j] != 1 && (ps[j + 1] - ps[at]) <= limit + 0.01f) {
        if (breakAll == 0 && kind[j] == 2) {
            lastSpace = j;
        }
        ++j;
    }
    int end = j;
    int next = j;
    if (j < n && kind[j] == 1) {
        next = j + 1;
    } else if (j < n && lastSpace > at) {
        end = lastSpace;
        next = lastSpace + 1;
    }
    if (end == at) {
        end = at + 1;
        next = end;
    }
    cut.end = end;
    cut.next = next;
    cut.width = ps[end] - ps[at];
    return cut;
}

void uiText(UiState& s, uint16_t id, const char* text);
float uiAdvance(const UiState& s, uint32_t cp);
float uiMeasure(const UiState& s, const char* text, uint8_t len, uint8_t until);
TextRun textRun(const UiState& s, const UiText& text, const UiBox& slot);
void textInk(const UiLook& look, float r, float g, float b, bool hot, float& tr, float& tg, float& tb);

uint32_t uiEmitText(
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

uint32_t uiEmitRun(
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

uint32_t uiEmitTextNode(
    UiState& s,
    uint16_t id,
    UiPrimitive* dst,
    uint32_t n,
    uint32_t cap,
    const UiBox* clip,
    float r,
    float g,
    float b,
    float a);

} // namespace burnhope

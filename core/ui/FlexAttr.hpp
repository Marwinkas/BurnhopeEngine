#pragma once

#include "ui/Flex.hpp"
#include <cstdint>

namespace burnhope {

// DTO для сериализации и внешних редакторов
struct UiFlexAttr {
    uint8_t direction = 0;
    uint8_t wrap = 0;
    uint8_t justify = 0;
    uint8_t align = 0;
    uint8_t widthMode = 0;
    uint8_t heightMode = 0;
    uint8_t position = 0;
    float width = 0.0f;
    float height = 0.0f;
    float grow = 0.0f;
    float shrink = 1.0f;
    float pad = 0.0f;
    float padL = 0.0f;
    float padR = 0.0f;
    float padT = 0.0f;
    float padB = 0.0f;
    float gap = 0.0f;
    float posX = 0.0f;
    float posY = 0.0f;
    float minW = 0.0f;
    float minH = 0.0f;
    float marginL = 0.0f;
    float marginR = 0.0f;
    float marginT = 0.0f;
    float marginB = 0.0f;
    uint8_t drop = 0;
    uint8_t content = 0;
    uint8_t self = 0;
    uint8_t basisMode = 0;
    uint8_t maxWMode = 0;
    uint8_t maxHMode = 0;
    uint8_t overflow = 0;
    uint8_t box = 0;
    uint8_t inset = 0;
    float basis = 0.0f;
    float maxW = 0.0f;
    float maxH = 0.0f;
    float gapRow = kUiGapUnset;
    float aspect = 0.0f;
    float posR = 0.0f;
    float posB = 0.0f;
};

void uiFlexApplyAttr(UiFlex& flex, const UiFlexAttr& attr) noexcept;
bool uiFlexApplyAttr(UiFlex& flex, const char* key, const char* word) noexcept;
bool uiFlexApplyAttr(UiFlex& flex, const char* key, float value) noexcept;
void uiFlexApplyAttr(UiFlex& flex, const char* key, uint8_t mode, float value) noexcept;

} // namespace burnhope
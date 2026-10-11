#pragma once

#include "ui/Model.hpp"

#include <yoga/Yoga.h>

namespace burnhope {

struct UiState;

YGFlexDirection flexDir(uint8_t direction);
YGAlign flexAlign(uint8_t align);
YGJustify flexJustify(uint8_t justify);
YGAlign flexContent(uint8_t content);
YGAlign flexSelf(uint8_t self);
void applyFlex(YGNodeRef node, const UiFlex& flex);
void cacheBox(UiState& s, uint16_t id);
void uiLayout(UiState& s, float w, float h);

// Flex attributes shared by the binary layout reader and the JSON UI builder.
struct UiFlexAttr {
    uint8_t direction = 0;
    uint8_t wrap = 0;
    uint8_t justify = 0;
    uint8_t align = 0;
    uint8_t widthMode = 0;
    uint8_t heightMode = 0;
    uint8_t position = 0;
    float width = 0;
    float height = 0;
    float grow = 0;
    float shrink = 0;
    float pad = 0;
    float gap = 0;
    float posX = 0;
    float posY = 0;
    float padT = 0;
    float padB = 0;
    float minW = 0;
    float minH = 0;
    float marginL = 0;
    float marginR = 0;
    float marginT = 0;
    float marginB = 0;
    uint8_t drop = 0;
    uint8_t content = 0;
    uint8_t self = 0;
    uint8_t basisMode = 0;
    uint8_t maxWMode = 0;
    uint8_t maxHMode = 0;
    uint8_t overflow = 0;
    uint8_t box = 0;
    uint8_t inset = 0;
    float basis = 0;
    float maxW = 0;
    float maxH = 0;
    float gapRow = -1;
    float aspect = 0;
    float posR = 0;
    float posB = 0;
};

void uiFlexApplyAttr(UiFlex& flex, const UiFlexAttr& attr);
bool uiFlexApplyAttr(UiFlex& flex, const char* key, const char* word);
bool uiFlexApplyAttr(UiFlex& flex, const char* key, float value);
void uiFlexApplyAttr(UiFlex& flex, const char* key, uint8_t mode, float value);

} // namespace burnhope

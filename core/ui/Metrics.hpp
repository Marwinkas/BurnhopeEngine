#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

// Default sizes and padding for every widget. Change the look here, not on each button.
struct UiMetrics {
    float buttonH = 28.0f;
    float buttonPadX = 12.0f;
    float fieldH = 36.0f;
    float searchH = 26.0f;
    float checkH = 36.0f;
    float sliderH = 18.0f;
    float menuItemH = 30.0f;
    float menuPadX = 10.0f;
    float menuShadow = 10.0f;
    float rowH = 28.0f;
    float rowGap = 4.0f;
    float treeIndent = 16.0f;
    float chevronW = 22.0f;
    float icon = 16.0f;
    float cellPad = 6.0f;
    float cellPadX = 7.0f;
    float cellPadT = 3.5f;
    float subH = 12.0f;
    float subScale = 0.8f;
    float cellGap = 4.0f;
    float listPad = 8.0f;
    float listGap = 6.0f;
    float gridInset = 8.0f;
    float labelH = 18.0f;
    float listRowH = 36.0f;
    float iconMin = 48.0f;
    float iconMax = 256.0f;
    float iconStep = 8.0f;
    float dragSlop = 6.0f;
    float ghostW = 36.0f;
    float ghostH = 22.0f;
    float ghostA = 0.88f;
    float selectedA = 0.92f;
    float headerA = 0.95f;
    float tuneW = 220.0f;
    float tuneH = 120.0f;
};

static_assert(std::is_trivially_copyable_v<UiMetrics>);

} // namespace burnhope

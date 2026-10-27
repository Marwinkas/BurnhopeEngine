#pragma once

#include "ui/Flex.hpp"
#include "ui/Geometry.hpp"
#include <cstdint>

namespace burnhope {

struct UiState;

// Константы геометрии и лимитов алгоритмов верстки
inline constexpr uint16_t kUiFlexDropVictimCap = 64;
inline constexpr float kUiLayoutEpsilon = 1.0f;
inline constexpr float kUiPixelSnap = 0.5f;

// Универсальный замер контента (текста, картинок) для Yoga.
// Yoga не знает о шрифтах или глифах — она просто запрашивает размеры у ноды через этот делегат.
using UiMeasureFn = void (*)(UiState& s, uint16_t id, float maxW, float& outW, float& outH, void* user);

struct UiContentMeasure {
    UiMeasureFn fn = nullptr;
    void* user = nullptr;
};

// Компонент обновления верстки: навешивается на сущности, которым требуется
// обновить или пересоздать дочерние элементы перед вычислением раскладки (виртуальные списки, деревья)
using UiLayoutRefreshFn = void (*)(UiState& s, uint16_t id, void* user);

struct UiLayoutRefresh {
    UiLayoutRefreshFn fn = nullptr;
    void* user = nullptr;
};

// Запись вычисленных координат ноды из Yoga в UiBox и UiBounds
void cacheBox(UiState& s, uint16_t id);

// Главный расчет верстки дерева под размер экрана (w, h)
void uiLayout(UiState& s, float w, float h);

} // namespace burnhope
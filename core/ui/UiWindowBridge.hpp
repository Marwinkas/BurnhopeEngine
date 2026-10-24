#pragma once

#include "ui/Interaction.hpp"
#include "ui/Style.hpp"
#include <cstdint>

namespace burnhope {

struct UiState;

// Извлечение запросов управления окном ОС (закрыть, свернуть, развернуть, прозрачность)
void uiWindowTakeOps(UiState& s, WindowOps& out);

// Извлечение скопированного интерфейсом текста для буфера обмена ОС
[[nodiscard]] bool uiWindowTakeClip(UiState& s, char* dst, uint8_t cap);

// Метрики заголовка и границ изменения размера окна из дизайн-токенов
void uiWindowChromeMetrics(const UiLook& look, float& titleBar, float& resizeBorder);

} // namespace burnhope
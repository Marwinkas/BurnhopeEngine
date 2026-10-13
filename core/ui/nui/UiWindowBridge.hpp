#pragma once

#include "core/platform/Input.hpp"
#include "core/ui/Interaction.hpp"
#include "core/ui/Style.hpp"
#include <cstdint>

namespace burnhope {

struct UiState;

// Извлечение запросов управления окном ОС (закрыть, свернуть, развернуть, прозрачность)
void uiWindowTakeOps(UiState& s, WindowOps& out);

// Извлечение скопированного интерфейсом текста для буфера обмена ОС
[[nodiscard]] bool uiWindowTakeClip(UiState& s, char* dst, uint8_t cap);

// Метрики заголовка и границ изменения размера окна из дизайн-токенов
void uiWindowChromeMetrics(const UiLook& look, float& titleBar, float& resizeBorder);

// Фильтрация ввода 3D-сцены: если фокус на поле ввода или курсор над UI,
// глушит клики, вращение камеры и ввод текста, чтобы игра не перехватывала управление.
void uiMaskSceneInput(const UiState& s, InputFrame& input);

} // namespace burnhope
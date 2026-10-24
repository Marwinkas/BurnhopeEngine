#pragma once

#include "ui/Geometry.hpp"
#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint16_t kUiCap = 2048;
constexpr uint8_t kUiCmdCap = 128;

// Индекс сущности в плоских массивах ядра
struct UiSlot {
    uint16_t index = 0;
};

// ============================================================================
// Чистые теги состояний Flecs (Компоненты нулевого размера)
// ============================================================================

struct UiHovered {};   // Мышь над сущностью
struct UiFocused {};   // Фокус ввода (каретка / клавиатура)
struct UiFocusable {}; // МОЖЕТ принимать фокус по клавише Tab (вместо enum ролей!)
struct UiCapture {};   // Захват мыши (drag)
struct UiTextInput {}; // Узел сам принимает текст и жест
struct UiPass {};      // Пропуск клика сквозь элемент

// ============================================================================
// Системные события и коллбэки
// ============================================================================

using UiCommandFn = void (*)(void* user, uint16_t item);
using UiClickFn   = void (*)(void* user, uint16_t id);
using UiValueFn   = void (*)(void* user, uint16_t id, float value);
using UiDragFn    = void (*)(void* user, uint16_t id, float x, float y);
using UiEditFn    = void (*)(void* user, uint16_t id);
using UiGrabFn    = bool (*)(void* user, uint16_t hit, float x, float y);
using UiFocusFn   = void (*)(void* user, uint16_t id);
using UiWheelFn   = void (*)(void* user, uint16_t id, float wheel, float x, float y);

struct UiAction  { UiClickFn fn = nullptr; void* user = nullptr; };
struct UiDrag    { UiDragFn  fn = nullptr; void* user = nullptr; };
struct UiWheel   { UiWheelFn fn = nullptr; void* user = nullptr; };
struct UiEdit    { UiEditFn  fn = nullptr; void* user = nullptr; };

struct UiPressed {
    uint8_t button = 0;
    float x = 0.0f;
    float y = 0.0f;
};

struct UiDragState {
    float startX = 0.0f;
    float startY = 0.0f;
    float currentX = 0.0f;
    float currentY = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    bool active = false;
};

struct UiHoverable {};
struct UiPopup {};
struct UiTab { uint16_t index = 0; };

enum class UiEvent : uint8_t { None = 0, Close = 1 };

inline constexpr uint8_t kUiInCursor = 1;
inline constexpr uint8_t kUiInEarly = 2;
inline constexpr uint8_t kUiInEscape = 3;
inline constexpr uint8_t kUiPhase4 = 4;
inline constexpr uint8_t kUiPhase5 = 5;

struct UiPhaseNote {
    uint8_t cursor = 0;
    uint8_t plugAte = 0;
    uint8_t took = 0;
};
inline constexpr uint8_t kUiInPress = 6;
inline constexpr uint8_t kUiInPressLate = 7;
inline constexpr uint8_t kUiInFocus = 8;
inline constexpr uint8_t kUiInDrag = 9;
inline constexpr uint8_t kUiInRelease = 10;
inline constexpr uint8_t kUiInWheel = 11;
inline constexpr uint8_t kUiInKey = 12;
inline constexpr uint8_t kUiInClip = 13;
inline constexpr uint8_t kUiInSync = 14;

struct WindowOps {
    bool close = false;
    bool minimize = false;
    bool toggleMaximize = false;
    int alwaysOnTop = -1;
    float opacity = -1.0f;
};

struct UiClipOut {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t put = 0;
};

struct UiIme {
    char bytes[64]{};
    uint8_t len = 0;
};

struct UiName {
    char bytes[32]{};
    uint8_t len = 0;
};

static_assert(std::is_trivially_copyable_v<UiSlot>);
static_assert(std::is_trivially_copyable_v<UiPressed>);
static_assert(std::is_trivially_copyable_v<UiDragState>);

} // namespace burnhope
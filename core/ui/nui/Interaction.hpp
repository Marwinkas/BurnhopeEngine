#pragma once

#include "core/ui/Geometry.hpp"
#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint16_t kUiCap = 1024;
constexpr uint8_t kUiCmdCap = 48;

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
struct UiPass {};      // Пропуск клика сквозь элемент

// ============================================================================
// Системные события и коллбэки
// ============================================================================

using UiCommandFn = void (*)(void* user);
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

struct UiCommand {
    char name[24]{};
    uint8_t len = 0;
    UiCommandFn fn = nullptr;
    void* user = nullptr;
};

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

struct UiDrop {
    uint16_t source = kUiNone;
    uint16_t target = kUiNone;
    char kind[16]{};
    char bytes[64]{};
    bool active = false;
    bool hover = false;
};

struct UiDropTarget {
    char kind[16]{};
};
struct UiTextInput {};

struct WindowOps {
    bool close = false;
    bool minimize = false;
    bool toggleMaximize = false;
    int alwaysOnTop = -1;
    float opacity = -1.0f;
};

struct UiName {
    char bytes[32]{};
    uint8_t len = 0;
};

static_assert(std::is_trivially_copyable_v<UiSlot>);
static_assert(std::is_trivially_copyable_v<UiPressed>);
static_assert(std::is_trivially_copyable_v<UiDragState>);
static_assert(std::is_trivially_copyable_v<UiCommand>);

} // namespace burnhope
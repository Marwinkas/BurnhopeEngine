#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kInputQueueCap = 128;
constexpr uint32_t kInputKeyBits = 512;
constexpr uint32_t kInputPadCap = 4;
constexpr uint32_t kInputTouchCap = 8;
constexpr uint32_t kInputKeyA = 4;

constexpr uint8_t kInputShift = 1u;
constexpr uint8_t kInputCtrl = 2u;
constexpr uint8_t kInputAlt = 4u;
constexpr uint8_t kInputPenEraser = 8u;
constexpr uint8_t kInputPenBarrel = 16u;

enum class InputDevice : uint8_t {
    Keyboard = 0,
    Mouse,
    Gamepad,
    Touch,
    TabletPen,
    SystemTray,
};

enum class InputEventType : uint8_t {
    KeyDown = 0,
    KeyUp,
    ButtonDown,
    ButtonUp,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    AxisMotion,
    Scroll,
    TouchDown,
    TouchMove,
    TouchUp,
    TabletPenMove,
    TabletPenDown,
    TabletPenUp,
    TrayIconClick,
    TrayMenuAction,
};

struct InputEvent {
    uint8_t device = 0;
    uint8_t type = 0;
    uint8_t deviceId = 0;
    uint8_t flags = 0;
    uint32_t code = 0;
    float x = 0.0f;
    float y = 0.0f;
    float axisX = 0.0f;
    float axisY = 0.0f;
    float pressure = 0.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    float tangentialPressure = 0.0f;
    uint8_t consumed = 0;
    uint8_t pad[3]{};
    uint32_t reserved = 0;
};

struct InputPad {
    float lx = 0.0f;
    float ly = 0.0f;
    float rx = 0.0f;
    float ry = 0.0f;
    float triggerL = 0.0f;
    float triggerR = 0.0f;
    uint32_t active = 0;
    uint32_t pressed = 0;
    uint32_t released = 0;
    uint32_t reserved = 0;
};

struct InputTouch {
    float x = 0.0f;
    float y = 0.0f;
    uint8_t id = 0;
    uint8_t active = 0;
    uint8_t pressed = 0;
    uint8_t released = 0;
    uint32_t reserved = 0;
};

struct InputState {
    uint64_t keyActive[8]{};
    uint64_t keyPressed[8]{};
    uint64_t keyReleased[8]{};
    uint64_t keyConsumed[8]{};
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
    float wheelDelta = 0.0f;
    float penX = 0.0f;
    float penY = 0.0f;
    float penPressure = 0.0f;
    float penTiltX = 0.0f;
    float penTiltY = 0.0f;
    float penTangential = 0.0f;
    uint32_t mouseActive = 0;
    uint32_t mousePressed = 0;
    uint32_t mouseReleased = 0;
    uint32_t trayCode = 0;
    uint32_t trayMenuCode = 0;
    uint8_t mouseValid = 0;
    uint8_t penActive = 0;
    uint8_t penPressed = 0;
    uint8_t penReleased = 0;
    uint8_t trayClicked = 0;
    uint8_t trayAction = 0;
    uint8_t modifiers = 0;
    uint8_t reserved = 0;
    uint32_t reserved1 = 0;
    uint32_t reserved2 = 0;
    InputPad pads[kInputPadCap]{};
    InputTouch touches[kInputTouchCap]{};
};

struct InputQueue {
    InputEvent events[kInputQueueCap]{};
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t count = 0;
};

static_assert(std::is_trivially_copyable_v<InputEvent>);
static_assert(sizeof(InputEvent) == 48);
static_assert(sizeof(InputEvent) % 16 == 0);
static_assert(std::is_trivially_copyable_v<InputState>);
static_assert(sizeof(InputState) % 16 == 0);
static_assert(std::is_trivially_copyable_v<InputQueue>);

inline bool inputBit(const uint64_t* words, uint32_t code) {
    if (words == nullptr || code >= kInputKeyBits) {
        return false;
    }
    return (words[code >> 6] & (1ull << (code & 63))) != 0;
}

inline void inputSetBit(uint64_t* words, uint32_t code) {
    if (words == nullptr || code >= kInputKeyBits) {
        return;
    }
    words[code >> 6] |= 1ull << (code & 63);
}

inline void inputClearBit(uint64_t* words, uint32_t code) {
    if (words == nullptr || code >= kInputKeyBits) {
        return;
    }
    words[code >> 6] &= ~(1ull << (code & 63));
}

inline bool inputKeyDown(const InputState& state, uint32_t code) {
    return inputBit(state.keyActive, code);
}

inline bool inputKeyPressed(const InputState& state, uint32_t code) {
    return inputBit(state.keyPressed, code);
}

inline bool inputKeyReleased(const InputState& state, uint32_t code) {
    return inputBit(state.keyReleased, code);
}

inline bool inputPushEvent(InputQueue& queue, const InputEvent& event) {
    if (queue.count >= kInputQueueCap) {
        return false;
    }
    queue.events[queue.tail] = event;
    queue.tail = (queue.tail + 1) % kInputQueueCap;
    queue.count += 1;
    return true;
}

inline void inputBeginFrame(InputState& state) {
    std::memset(state.keyPressed, 0, sizeof(state.keyPressed));
    std::memset(state.keyReleased, 0, sizeof(state.keyReleased));
    std::memset(state.keyConsumed, 0, sizeof(state.keyConsumed));
    state.deltaX = 0.0f;
    state.deltaY = 0.0f;
    state.wheelDelta = 0.0f;
    state.mousePressed = 0;
    state.mouseReleased = 0;
    state.penPressed = 0;
    state.penReleased = 0;
    state.trayClicked = 0;
    state.trayAction = 0;
    for (uint32_t i = 0; i < kInputPadCap; ++i) {
        state.pads[i].pressed = 0;
        state.pads[i].released = 0;
    }
    for (uint32_t i = 0; i < kInputTouchCap; ++i) {
        state.touches[i].pressed = 0;
        state.touches[i].released = 0;
    }
}

inline void inputMovePointer(InputState& state, float x, float y) {
    if (state.mouseValid != 0) {
        state.deltaX += x - state.mouseX;
        state.deltaY += y - state.mouseY;
    }
    state.mouseX = x;
    state.mouseY = y;
    state.mouseValid = 1;
}

inline void inputApplyButton(uint32_t& active, uint32_t& pressed, uint32_t& released, uint32_t code, bool down) {
    if (code >= 32) {
        return;
    }
    const uint32_t bit = 1u << code;
    if (down) {
        active |= bit;
        pressed |= bit;
    } else {
        active &= ~bit;
        released |= bit;
    }
}

inline InputTouch* inputTouchSlot(InputState& state, uint8_t id) {
    for (uint32_t i = 0; i < kInputTouchCap; ++i) {
        if (state.touches[i].active != 0 && state.touches[i].id == id) {
            return &state.touches[i];
        }
    }
    for (uint32_t i = 0; i < kInputTouchCap; ++i) {
        if (state.touches[i].active == 0 && state.touches[i].pressed == 0) {
            state.touches[i].id = id;
            return &state.touches[i];
        }
    }
    return nullptr;
}

inline void inputApplyEvent(InputState& state, const InputEvent& event) {
    state.modifiers = event.flags;
    const auto type = static_cast<InputEventType>(event.type);
    if (type == InputEventType::KeyDown || type == InputEventType::KeyUp) {
        const bool down = type == InputEventType::KeyDown;
        if (down) {
            inputSetBit(state.keyActive, event.code);
            inputSetBit(state.keyPressed, event.code);
        } else {
            inputClearBit(state.keyActive, event.code);
            inputSetBit(state.keyReleased, event.code);
        }
        if (event.consumed != 0) {
            inputSetBit(state.keyConsumed, event.code);
        }
        return;
    }
    if (type == InputEventType::MouseMove) {
        inputMovePointer(state, event.x, event.y);
        return;
    }
    if (type == InputEventType::MouseButtonDown || type == InputEventType::MouseButtonUp) {
        inputApplyButton(state.mouseActive, state.mousePressed, state.mouseReleased, event.code, type == InputEventType::MouseButtonDown);
        inputMovePointer(state, event.x, event.y);
        return;
    }
    if (type == InputEventType::Scroll) {
        state.wheelDelta += event.axisY;
        return;
    }
    if (event.deviceId >= kInputPadCap) {
        if (type == InputEventType::ButtonDown || type == InputEventType::ButtonUp || type == InputEventType::AxisMotion) {
            return;
        }
    }
    if (type == InputEventType::ButtonDown || type == InputEventType::ButtonUp) {
        InputPad& pad = state.pads[event.deviceId];
        inputApplyButton(pad.active, pad.pressed, pad.released, event.code, type == InputEventType::ButtonDown);
        return;
    }
    if (type == InputEventType::AxisMotion) {
        InputPad& pad = state.pads[event.deviceId];
        if (event.code == 1) {
            pad.rx = event.axisX;
            pad.ry = event.axisY;
        } else if (event.code == 2) {
            pad.triggerL = event.axisX;
            pad.triggerR = event.axisY;
        } else {
            pad.lx = event.axisX;
            pad.ly = event.axisY;
        }
        return;
    }
    if (type == InputEventType::TouchDown || type == InputEventType::TouchMove || type == InputEventType::TouchUp) {
        InputTouch* slot = inputTouchSlot(state, event.deviceId);
        if (slot == nullptr) {
            return;
        }
        slot->x = event.x;
        slot->y = event.y;
        slot->id = event.deviceId;
        if (type == InputEventType::TouchUp) {
            slot->active = 0;
            slot->released = 1;
        } else {
            slot->active = 1;
            if (type == InputEventType::TouchDown) {
                slot->pressed = 1;
            }
        }
        return;
    }
    if (type == InputEventType::TabletPenMove || type == InputEventType::TabletPenDown || type == InputEventType::TabletPenUp) {
        state.penX = event.x;
        state.penY = event.y;
        state.penPressure = event.pressure;
        state.penTiltX = event.tiltX;
        state.penTiltY = event.tiltY;
        state.penTangential = event.tangentialPressure;
        if (type == InputEventType::TabletPenUp) {
            state.penActive = 0;
            state.penReleased = 1;
        } else if (type == InputEventType::TabletPenDown || event.pressure > 0.0f) {
            state.penActive = 1;
            if (type == InputEventType::TabletPenDown) {
                state.penPressed = 1;
            }
        }
        return;
    }
    if (type == InputEventType::TrayIconClick) {
        state.trayClicked = 1;
        state.trayCode = event.code;
        return;
    }
    if (type == InputEventType::TrayMenuAction) {
        state.trayAction = 1;
        state.trayMenuCode = event.code;
    }
}

inline void inputProcessQueue(InputState& state, InputQueue& queue) {
    uint32_t index = queue.head;
    for (uint32_t n = 0; n < queue.count; ++n) {
        inputApplyEvent(state, queue.events[index]);
        index = (index + 1) % kInputQueueCap;
    }
    queue.head = 0;
    queue.tail = 0;
    queue.count = 0;
}

inline InputEvent inputMake(InputDevice device, InputEventType type) {
    InputEvent event{};
    event.device = static_cast<uint8_t>(device);
    event.type = static_cast<uint8_t>(type);
    return event;
}

} // namespace burnhope

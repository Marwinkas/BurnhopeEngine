#pragma once

#include "ui/State.hpp"

#include <span>

namespace burnhope {

inline constexpr uint32_t kUiDragNone = 0xFFFFFFFFu;

struct UiDragHost {};

struct UiDragDrop {
    uint16_t ghost = kUiNone;
    uint16_t mark = kUiNone;
    uint16_t srcHost = kUiNone;
    uint32_t srcIndex = kUiDragNone;
    float x = 0.0f;
    float y = 0.0f;
    float dropX = 0.0f;
    float dropY = 0.0f;
    uint8_t armed = 0;
    uint8_t live = 0;
    uint8_t (*canDrag)(void* user, uint16_t host, uint32_t index) = nullptr;
    uint8_t (*canDrop)(void* user, uint16_t host, uint32_t index) = nullptr;
    void (*onDrop)(void* user, uint16_t srcHost, uint32_t srcIndex, uint16_t dstHost, uint32_t dstIndex) = nullptr;
    void (*badge)(void* user, char* dst, uint8_t cap) = nullptr;
    void* user = nullptr;
};

struct UiDragDropConfig {
    std::span<const uint16_t> hosts{};
    uint8_t (*canDrag)(void* user, uint16_t host, uint32_t index) = nullptr;
    uint8_t (*canDrop)(void* user, uint16_t host, uint32_t index) = nullptr;
    void (*onDrop)(void* user, uint16_t srcHost, uint32_t srcIndex, uint16_t dstHost, uint32_t dstIndex) = nullptr;
    void (*badge)(void* user, char* dst, uint8_t cap) = nullptr;
    void* user = nullptr;
};

void uiDragDropBind(UiState& s, const UiDragDropConfig& cfg);
void uiDragAddHost(UiState& s, uint16_t host);
bool uiDragDropInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope

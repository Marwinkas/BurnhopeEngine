#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiVirtual {
    uint32_t count = 0;
    uint32_t first = 0;
    uint16_t slot[kUiPoolCap]{};
    uint8_t slots = 0;
    float rowH = 36;
    void (*fill)(void* user, uint32_t index, char* dst, uint8_t cap) = nullptr;
    void (*click)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
};

struct UiVirtRow {
    uint32_t index = 0;
};

uint16_t uiVirtual(UiState& s, uint16_t parent, float rowH);
void uiVirtualSource(UiState& s, uint16_t id, uint32_t count, void (*fill)(void*, uint32_t, char*, uint8_t), void (*click)(void*, uint32_t), void* user);
void uiVirtualRefresh(UiState& s, uint16_t id);

} // namespace burnhope

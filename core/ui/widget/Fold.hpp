#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiFold {
    uint16_t body = kUiNone;
    uint16_t clip = kUiNone;
    uint16_t next = kUiNone;
    uint8_t open = 0;
    uint8_t pad = 0;
    float full = 0.0f;
};

struct UiFoldChain {
    uint16_t head = kUiNone;
};

static_assert(std::is_trivially_copyable_v<UiFold>);

uint16_t uiFoldBody(UiState& s, uint16_t parent, const char* title);
uint16_t uiFoldAttach(UiState& s, uint16_t parent, uint16_t head);
void uiFoldOpen(UiState& s, uint16_t body);
void uiFoldToggle(UiState& s, uint16_t head);

} // namespace burnhope

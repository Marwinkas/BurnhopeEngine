#pragma once

#include "ui/State.hpp"

namespace burnhope {

// One menu row. `item` is the index passed into the command. `owner` is a choice button.
struct UiMenuCmd {
    uint16_t item = 0;
    uint16_t command = kUiNone;
    uint16_t owner = kUiNone;
    UiCommandFn fn = nullptr;
    void* user = nullptr;
};

static_assert(std::is_trivially_copyable_v<UiMenuCmd>);

struct UiMenuMetrics {
    uint16_t opened = kUiNone;
    uint16_t flyout = kUiNone;
    float w = 180.0f;
    float itemH = 28.0f;
    float pad = 6.0f;
    float gap = 4.0f;
};

struct UiMenuHooks {
    void (*invoke)(void* user, uint16_t id) = nullptr;
};

inline UiMenuMetrics& uiMenuMetrics(UiState& s) {
    return s.world.ensure<UiMenuMetrics>();
}

inline UiMenuHooks& uiMenuHooks(UiState& s) {
    return s.world.ensure<UiMenuHooks>();
}

void uiMenuRowBind(UiState& s, uint16_t row, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command);
uint16_t uiMenuRow(UiState& s, uint16_t menu, const char* label, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command);
bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user);
bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user);

} // namespace burnhope

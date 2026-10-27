#pragma once

#include "ui/Interaction.hpp"
#include "ui/Types.hpp"

#include <cstdint>
#include <type_traits>

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
    float w = 0.0f; // filled from the "menu" class unless uiMenuStyle changed them
    float itemH = 0.0f;
    float pad = 0.0f;
    float gap = 0.0f;
};

struct UiMenuHooks {
    void (*invoke)(void* user, uint16_t id) = nullptr;
};

// Cap of menu commands attached through uiCommandAdd. The row stores the callback.
// The old UiState array copied name and function and nothing read the copy back.
struct UiCommandList {
    uint8_t count = 0;
};

static_assert(std::is_trivially_copyable_v<UiCommandList>);

void uiCommandBind(UiState& s);
void uiCommandStore(UiState& s, uint16_t row, UiCommandFn fn, void* user, uint16_t item, uint16_t owner, uint16_t command);

UiMenuHooks& uiMenuHooks(UiState& s);

bool uiCommandAdd(UiState& s, const char* name, UiCommandFn fn, void* user);
bool uiCommandAddTo(UiState& s, uint16_t menu, const char* name, UiCommandFn fn, void* user);

} // namespace burnhope

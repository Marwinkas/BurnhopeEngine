#pragma once

#include "host/PropGrid.hpp"

#include <cstdint>
#include <deque>
#include <vector>

namespace burnhope {

enum BindKind : uint8_t {
    kBindFloat = 0,
    kBindFlag = 1,
    kBindSwatch = 2,
};

// One row per control. index is a scalar or a flag in the window block.
// A swatch owns three scalars starting at index. The table grows while the
// layout is built and is only walked during the frame.
struct UiBind {
    uint16_t id = kUiNone;
    uint16_t index = 0;
    uint8_t kind = kBindFloat;
    float fallback = 0.0f;
};

struct UiPropBinder;

struct UiMenuPick {
    UiPropBinder* bind = nullptr;
    uint16_t button = kUiNone;
    uint16_t index = 0;
    uint8_t value = 0;
    char label[48]{};
    void (*onFlag)(void* user, uint16_t index, uint8_t value, uint16_t button, const char* label) = nullptr;
    void* user = nullptr;
};

struct UiCommandPick {
    void (*fn)(void* user, uint16_t item) = nullptr;
    void* user = nullptr;
    uint16_t arg = 0;
};

// op 0: flag == value. op 1: flag != value.
struct UiShowRule {
    uint16_t node = kUiNone;
    uint16_t flag = 0;
    uint8_t value = 0;
    uint8_t op = 0;
};

struct UiSlideSpec {
    const char* label = nullptr;
    uint16_t index = 0;
    float def = 0.0f;
    float minV = 0.0f;
    float maxV = 1.0f;
};

struct UiPropBinder {
    float* scalars = nullptr;
    uint8_t* flags = nullptr;
    uint16_t scalarN = 0;
    uint16_t flagN = 0;
    std::vector<UiBind> rows;
    std::vector<UiShowRule> shown;
    std::deque<UiMenuPick> menus;
    std::deque<UiCommandPick> commands;

    void attach(float* scalars, uint16_t scalarCount, uint8_t* flags, uint16_t flagCount);
    void clear();
    void show(uint16_t node, uint16_t flag, uint8_t value, uint8_t op);
    uint16_t slide(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV);
    void watch(uint16_t id, uint16_t index, float fallback);
    uint16_t slideStep(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV, float step);
    void slides(UiState& s, uint16_t panel, const UiSlideSpec* spec, uint16_t count);
    uint16_t check(UiState& s, uint16_t panel, const char* label, uint16_t index, bool def);
    uint16_t swatch(UiState& s, uint16_t panel, const char* label, uint16_t index, float r0, float g0, float b0);
    void sync(const UiState& s) const;
    void syncSwatches(UiState& s);
    void write(UiState& s, uint16_t index, float value) const;
    [[nodiscard]] uint16_t idOf(uint16_t index, uint8_t kind) const;
};

void onBinderMenu(void* user, uint16_t item);
void onBinderCommand(void* user, uint16_t item);
void propApplyShow(UiState& state, const UiPropBinder& bind);

} // namespace burnhope

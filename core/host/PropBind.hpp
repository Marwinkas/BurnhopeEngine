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
// A float row built by sliderNumber also owns `pair`, the slider that mirrors it. `last` is the slider value
// seen at the previous sync, so a slider the user moved is told apart from one that only followed the number.
struct UiBind {
    uint16_t id = kUiNone;
    uint16_t index = 0;
    uint8_t kind = kBindFloat;
    float fallback = 0.0f;
    uint16_t pair = kUiNone;
    float last = 0.0f;
};

// Three consecutive scalars shown as one vec3 row. While the row's lock is on, one axis edited alone moves the
// other two with it (by the same delta, or by the same ratio when proportional). `last` is the value seen at the
// previous sync.
struct UiVecLink {
    uint16_t index = 0;
    uint16_t lock = kUiNone;
    uint8_t proportional = 0;
    float last[3]{};
};

// What a reset button restores. Float: one scalar. Flag: one flag. Vec2: two scalars. Rgb: three scalars, all to `home`.
// Custom restores nothing itself and leaves the work to onReset.
enum class UiReset : uint8_t { Float, Flag, Vec2, Rgb, Custom };

// Sits on the button built by resetBtn.
struct UiResetHome {
    uint16_t index = 0;
    float home = 0.0f;
    float home2 = 0.0f;
    uint8_t kind = 0;
};

// Runs after the binder has restored the scalars of a reset button, for side effects the binder does not know.
using UiResetFn = void (*)(void* user, UiState& s, const UiResetHome& home);

struct UiPropBinder;

// Sits on the hex field built by colorHex: where the colour lives and who to tell when the text is edited.
struct UiHexBind {
    UiPropBinder* bind = nullptr;
    bool* dirty = nullptr;
    uint16_t chip = kUiNone;
    uint16_t rgb = 0;
    uint16_t alpha = 0;
    uint8_t withAlpha = 0;
};

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
    UiState* ui = nullptr;
    std::vector<UiBind> rows;
    std::vector<UiVecLink> vecs;
    std::vector<uint16_t> hexFields;
    std::vector<UiShowRule> shown;
    std::deque<UiMenuPick> menus;
    std::deque<UiCommandPick> commands;
    UiResetFn onReset = nullptr;
    void* resetUser = nullptr;

    void attach(float* scalars, uint16_t scalarCount, uint8_t* flags, uint16_t flagCount);
    void clear();
    void show(uint16_t node, uint16_t flag, uint8_t value, uint8_t op);
    uint16_t slide(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV);
    void watch(uint16_t id, uint16_t index, float fallback);
    uint16_t slideStep(UiState& s, uint16_t panel, const char* label, uint16_t index, float def, float minV, float maxV, float step);
    void slides(UiState& s, uint16_t panel, const UiSlideSpec* spec, uint16_t count);
    uint16_t check(UiState& s, uint16_t panel, const char* label, uint16_t index, bool def);
    // Clicking the swatch loads its three scalars into the colour book and opens the picker.
    uint16_t swatch(UiState& s, uint16_t panel, const char* label, uint16_t index, float r0, float g0, float b0);
    UiPropVec3 vec3(UiState& s, uint16_t host, const char* label, uint16_t index, float x, float y, float z, float minV, float maxV, float step, bool proportional = false);
    // A slider and a number sharing one scalar. Returns the number; whichever the user moves drives the other.
    uint16_t sliderNumber(UiState& s, uint16_t parent, uint16_t index, float def, float lo, float hi, float step);
    // A 48x24 swatch beside an 86x24 #RRGGBB field on scalars rgbIndex..+2. withAlpha adds the scalar at alphaIndex as #RRGGBBAA
    // while it is below 1. *outDirty is set when the user edits the text. Returns the swatch.
    uint16_t colorHex(UiState& s, uint16_t parent, uint16_t rgbIndex, uint16_t alphaIndex, bool withAlpha, bool* outDirty = nullptr);
    // A small reset button under `parent`: clicking it restores the scalars or flag at `index` to `home` (and `home2`
    // for the second axis of a Vec2), then calls onReset. Returns the button.
    uint16_t resetBtn(UiState& s, uint16_t parent, UiReset kind, uint16_t index, float home, float home2 = 0.0f);
    // A check row with a reset button in its value slot.
    uint16_t checkReset(UiState& s, uint16_t panel, const char* label, uint16_t flag, bool def);
    // A two-entry choice row bound to `flag` with a reset button. Returns the choice.
    uint16_t choiceReset(UiState& s, uint16_t panel, const char* label, const char* const* names, uint8_t count, void (*onPick)(void* user, uint8_t index), void* user, uint16_t flag);
    // A two-axis number row on scalars ix and ix + 1 with a reset button on the row.
    UiPropVec2 vec2Reset(UiState& s, uint16_t panel, const char* label, uint16_t ix, float x, float y, float minV = -64.0f, float maxV = 64.0f, float step = 0.01f);
    void sync(UiState& s);
    void syncSwatches(UiState& s);
    void write(UiState& s, uint16_t index, float value);
    [[nodiscard]] uint16_t idOf(uint16_t index, uint8_t kind) const;
};

void onBinderMenu(void* user, uint16_t item);
void onBinderCommand(void* user, uint16_t item);
void propApplyShow(UiState& state, const UiPropBinder& bind);

} // namespace burnhope

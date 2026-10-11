#pragma once

#include "ui/State.hpp"

namespace burnhope {

struct UiTextHook {
    void (*fn)(void* user, const char* text) = nullptr;
    void* user = nullptr;
};

struct UiFieldUndo {
    struct Snap {
        uint16_t id = kUiNone;
        char bytes[96]{};
        uint8_t len = 0;
        uint8_t caret = 0;
        uint8_t anchor = 0;
    } snap[8]{};
    uint8_t n = 0;
    uint8_t at = 0;
};

inline UiFieldUndo& uiUndo(UiState& s) {
    return s.world.ensure<UiFieldUndo>();
}

using UiTextChangeFn = void (*)(void* user, const char* text);

uint16_t uiField(UiState& s, uint16_t parent, const char* text, uint8_t filter, bool clear);
void uiBindField(UiState& s, uint16_t id, const char* text, uint8_t filter, bool clear);
void uiBindFieldExtra(UiState& s, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password);
void uiFieldNotify(UiState& s, uint16_t id);
uint16_t uiSearchField(UiState& s, uint16_t parent, const char* hint, UiTextChangeFn onChange, void* user, float width = 160.0f);
void uiSetFieldText(UiState& s, uint16_t id, const char* text);
void uiClearField(UiState& s);
void fieldPlaceText(UiState& s);
UiBox fieldInset(const UiState& s, uint16_t id, const UiField& field);
void uiFieldMulti(UiState& s, uint16_t id);

struct FieldRow {
    uint8_t begin = 0;
    uint8_t end = 0;
    float width = 0;
};

int fieldRows(const UiState& s, const UiField& field, float viewW, FieldRow* rows, int cap);
float fieldOffset(const UiState& s, const UiField& field, uint8_t from, uint8_t to);
uint8_t fieldCaretAt2(const UiState& s, const UiField& field, float localX, float localY, float viewW);

} // namespace burnhope

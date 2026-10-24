#pragma once

#include "ui/State.hpp"
#include "ui/widget/BinSlots.hpp"

namespace burnhope {

enum class UiFieldFilter : uint8_t {
    Any = 0,
    Digits = 1,
    Letters = 2,
    Email = 3,
    Tel = 4,
    Url = 5,
};

struct UiNamedField {};
struct UiFieldMenuMark {};
struct UiFieldCaret {};
struct UiFieldPaste {
    char bytes[96]{};
    uint8_t len = 0;
};

static_assert(std::is_trivially_copyable_v<UiFieldPaste>);
static_assert(sizeof(UiFieldPaste::bytes) == 96);

void uiSetField(UiState& s, uint16_t id);
uint16_t uiField(const UiState& s);
void uiSetFieldMenu(UiState& s, uint16_t id);
uint16_t uiFieldMenu(const UiState& s);

void uiFieldSetEdit(UiState& s, uint16_t id);
uint16_t uiFieldEdit(const UiState& s);

struct UiField {
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t caret = 0;
    uint8_t anchor = 0;
    uint8_t filter = 0;
    uint8_t clear = 0;
    uint8_t required = 0;
    uint8_t readOnly = 0;
    uint8_t password = 0;
    uint8_t inForm = 0;
    uint8_t maxChars = 0;
    uint8_t hintLen = 0;
    uint8_t multi = 0;
    uint8_t align = 0;
    uint8_t scrub = 0;
    float scroll = 0.0f;
    float padL = 12.0f;
    float padR = 12.0f;
    char hint[40]{};
};

inline float uiFieldWidth(const UiState& s, const UiField& field, uint8_t until) {
    if (field.password == 0) {
        return uiMeasure(s, field.bytes, field.len, until);
    }
    uint8_t count = 0;
    uint8_t i = 0;
    const uint8_t end = until < field.len ? until : field.len;
    while (i < end) {
        uint8_t step = 1;
        uiReadUtf8(field.bytes, field.len, i, step);
        if (step == 0) {
            break;
        }
        ++count;
        i = static_cast<uint8_t>(i + step);
    }
    return static_cast<float>(count) * uiAdvance(s, static_cast<uint32_t>('*'));
}

struct UiTextHook {
    void (*fn)(void* user, const char* text) = nullptr;
    void* user = nullptr;
};

struct UiFieldSnap {
    flecs::entity prev{};
    flecs::entity next{};
    char bytes[96]{};
    uint8_t len = 0;
    uint8_t caret = 0;
    uint8_t anchor = 0;
};

struct UiFieldUndo {
    flecs::entity head{};
    uint16_t n = 0;
    uint16_t at = 0;
};

using UiTextChangeFn = void (*)(void* user, const char* text);

struct UiAfterText {
    void (*fn)(UiState& s, uint16_t id) = nullptr;
};

void uiAfterTextAdd(UiState& s, uint16_t id, void (*fn)(UiState& s, uint16_t id));
void uiAfterText(UiState& s, uint16_t id);

uint16_t uiField(UiState& s, uint16_t parent, const char* text, uint8_t filter, bool clear);
void uiFieldBin(UiState& s);

void uiBindField(UiState& s, uint16_t id, const char* text, uint8_t filter, bool clear);
void uiBindFieldExtra(UiState& s, uint16_t id, const char* placeholder, uint8_t maxChars, bool required, bool readOnly, bool password);
void uiFieldNotify(UiState& s, uint16_t id);
void uiSetFieldText(UiState& s, uint16_t id, const char* text);
void uiFieldAlign(UiState& s, uint16_t id, uint8_t align);
bool fieldHandlePointer(UiState& s, uint16_t id, const InputFrame& in, bool press, bool down);
void uiClearField(UiState& s);
void fieldSyncText(UiState& s, uint16_t id);
void uiFieldInit(
    UiState& s,
    uint16_t id,
    const char* text,
    const char* hint = nullptr,
    uint8_t filter = 0,
    uint8_t flags = 0,
    uint8_t maxChars = 0,
    float padL = 0.0f,
    float padR = 0.0f);
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
bool fieldHandlePointer(UiState& s, uint16_t id, const InputFrame& in, bool press, bool down);
bool fieldHandleKey(UiState& s, uint16_t id, const InputFrame& in);
bool fieldAllows(uint8_t filter, uint32_t cp);
bool fieldCheck(uint8_t filter, const char* bytes, uint8_t len, const char*& msg);
bool fieldProblem(const UiField& field, const char*& msg);
void fieldMark(UiState& s, uint16_t id);
uint8_t fieldChars(const UiField& field);
void fieldSelectWord(UiField& field, uint8_t at);
void fieldDropSel(UiField& field);
void fieldInsert(UiField& field, const char* src, uint8_t n);
void fieldInsert(UiState& s, uint16_t id, const char* src, uint8_t n);
void fieldRemember(UiState& s, uint16_t id, const UiField& field);
bool fieldUndo(UiState& s);
bool fieldRedo(UiState& s);
void fieldEraseBack(UiField& field);
void fieldEraseForward(UiField& field);
void fieldEraseBack(UiState& s, uint16_t id);
void fieldEraseForward(UiState& s, uint16_t id);
void fieldReveal(const UiState& s, UiField& field, float view);
uint8_t fieldCaretAt(const UiState& s, const UiField& field, float local);
void fieldCopy(UiState& s, const UiField& field);
UiField* editFieldOf(UiState& s);
void cmdFieldCut(void* user, uint16_t item);
void cmdFieldCopy(void* user, uint16_t item);
void cmdFieldPaste(void* user, uint16_t item);
void cmdFieldAll(void* user, uint16_t item);
void cmdFieldWipe(void* user, uint16_t item);
bool uiFieldInput(UiState& s, const InputFrame& in, uint16_t id, uint8_t phase, UiPhaseNote& note);

} // namespace burnhope

#pragma once

#include "ui/State.hpp"

namespace burnhope {

inline constexpr float kAccent[3][3] = {
    {0.36f, 0.55f, 0.92f},
    {0.45f, 0.72f, 0.48f},
    {0.86f, 0.55f, 0.32f},
};

inline void copyText(char* dst, uint8_t& len, const char* src) {
    uint8_t n = 0;
    if (src != nullptr) {
        while (src[n] != '\0' && n < 31) {
            dst[n] = src[n];
            ++n;
        }
    }
    dst[n] = '\0';
    len = n;
}

inline UiFlex px(float w, float h) {
    UiFlex f{};
    f.widthMode = static_cast<uint8_t>(UiSize::Px);
    f.heightMode = static_cast<uint8_t>(UiSize::Px);
    f.width = w;
    f.height = h;
    f.shrink = 0.0f;
    return f;
}

inline UiPaint paint(UiRole role, float r, float g, float b, float radius = 0.0f, int16_t tag = -1) {
    UiPaint out{};
    out.r = r;
    out.g = g;
    out.b = b;
    out.a = 1.0f;
    out.radius = radius;
    out.role = static_cast<uint8_t>(role);
    out.tag = tag;
    return out;
}

inline bool hitBox(const UiBox& b, float x, float y) {
    return b.w > 0.5f && b.h > 0.5f && x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h;
}

inline bool focusable(uint8_t role) {
    return role == static_cast<uint8_t>(UiRole::Button) || role == static_cast<uint8_t>(UiRole::Slider)
        || role == static_cast<uint8_t>(UiRole::Check) || role == static_cast<uint8_t>(UiRole::Field)
        || role == static_cast<uint8_t>(UiRole::Close);
}

uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);
void addText(UiState& s, uint16_t id, const char* text);
void tokenRgb(const UiLook& look, UiToken token, float& r, float& g, float& b);
void stamp(UiState& s, uint16_t id, UiToken token);
uint16_t hitTest(const UiState& s, float x, float y);
void setNote(UiState& s, const char* msg);
int daysInMonth(int year, int month);
int weekDay(int year, int month, int day);
void calFill(UiState& s);
void writeDate(UiState& s, int day);
void calPick(UiState& s, uint16_t id);
void calOnOpen(void* user, uint16_t id, int16_t tag);
void calOnToday(void* user, uint16_t id, int16_t tag);
void calOnPrev(void* user, uint16_t id, int16_t tag);
void calOnNext(void* user, uint16_t id, int16_t tag);
void calOnClose(void* user, uint16_t id, int16_t tag);
void calOnDay(void* user, uint16_t id, int16_t tag);
void colorOnOpen(void* user, uint16_t id, int16_t tag);
void activate(UiState& s, uint16_t id);
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b);
void rgbToHsv(float r, float g, float b, float& h, float& s, float& v);
void setRange(UiState& s, uint16_t id, float value);
int hexNib(char ch);
void writeHex(char* dst, uint8_t& len, float r, float g, float b, float a, bool withAlpha);
void applyColor(UiState& s, bool fromHsv, bool publish = true);
void takeBook(UiState& s);
void paintColor(UiState& s);
void dragWheel(UiState& s, uint16_t id, float x, float y);
bool readHex(const UiField& field, float& r, float& g, float& b, float& a);
void writeInt(char* dst, uint8_t& len, int v);
bool readInt(const UiField& field, int& out);
bool nodeShown(const UiState& s, uint16_t id);
void samplePixel(const UiState& s, float x, float y, float& r, float& g, float& b, float& a);
void dragSlider(UiState& s, uint16_t id, float x);
void colorTakeSlider(UiState& s, uint16_t id);
void colorOnValue(void* user, uint16_t id, float);
uint16_t splitTarget(const UiState& s, uint16_t split);
void scrollBy(UiState& s, uint16_t id, float wheel);
uint16_t scrollOwner(const UiState& s, uint16_t id);
bool fieldAllows(uint8_t filter, uint32_t cp);
bool emailOk(const UiField& field);
bool urlOk(const UiField& field);
bool telOk(const UiField& field);
bool fieldProblem(const UiField& field, const char*& msg);
void fieldMark(UiState& s, uint16_t id);
uint8_t fieldChars(const UiField& field);
void fieldSelectWord(UiField& field, uint8_t at);
void fieldDropSel(UiField& field);
void fieldInsert(UiField& field, const char* src, uint8_t n);
void fieldEraseBack(UiField& field);
void fieldEraseForward(UiField& field);
void fieldReveal(const UiState& s, UiField& field, float view);
uint8_t fieldCaretAt(const UiState& s, const UiField& field, float local);
void fieldCopy(UiState& s, const UiField& field);
void publishColor(UiState& s);
void setBtnLabel(UiState& s, uint16_t id, const char* text);
void refreshColorChrome(UiState& s);
void placeColorMenu(UiState& s);
void syncColor(UiState& s);
void photoClamp(UiImage& image);
bool photoMinimap(const UiBox& box, float x, float y, float& u, float& v);
void photoZoom(UiState& s, uint16_t id, float wheel, float x, float y);
void photoPan(UiState& s, uint16_t id, float x, float y);
void tickPhoto(UiState& s);
UiField* editFieldOf(UiState& s);
void cmdFieldCut(void* user);
void cmdFieldCopy(void* user);
void cmdFieldPaste(void* user);
void cmdFieldAll(void* user);
void cmdFieldWipe(void* user);
uint16_t listItemOf(const UiState& s, uint16_t id);
void moveListItem(UiState& s, uint16_t item, uint16_t before, bool after);
void fillColorBody(UiState& s, uint16_t parent);

} // namespace burnhope

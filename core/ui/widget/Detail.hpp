#pragma once

#include "ui/State.hpp"

#include <yoga/Yoga.h>
#include "ui/widget/Breadcrumbs.hpp"
#include "ui/widget/Button.hpp"
#include "ui/widget/Chrome.hpp"
#include "ui/widget/Curve.hpp"
#include "ui/widget/Field.hpp"
#include "ui/widget/Image.hpp"
#include "ui/widget/List.hpp"
#include "ui/widget/Menu.hpp"
#include "ui/widget/Rubberband.hpp"
#include "ui/widget/Scroll.hpp"
#include "ui/widget/Slider.hpp"
#include "ui/widget/Splitter.hpp"
#include "ui/widget/Tip.hpp"
#include "ui/widget/TreeView.hpp"
#include "ui/widget/Virtual.hpp"
#include "ui/widget/VirtualGrid.hpp"

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

void tokenRgb(const UiLook& look, UiToken token, float& r, float& g, float& b);

inline UiPaint paintToken(const UiLook& look, UiRole role, UiToken token, float radius = 0.0f, int16_t tag = -1) {
    UiPaint p = paint(role, 0.0f, 0.0f, 0.0f, radius, tag);
    p.token = static_cast<uint8_t>(token);
    tokenRgb(look, token, p.r, p.g, p.b);
    return p;
}

struct UiVirtualWindow {
    uint32_t first = 0;
    uint32_t visibleCount = 0;
    float totalHeight = 0.0f;
};

inline UiVirtualWindow uiVirtualCalc(float scrollOffset, float viewH, float itemH, uint32_t totalItems, uint8_t cols = 1) {
    UiVirtualWindow out{};
    if (cols < 1) {
        cols = 1;
    }
    if (itemH < 1.0f) {
        itemH = 1.0f;
    }
    if (scrollOffset < 0.0f) {
        scrollOffset = 0.0f;
    }
    if (viewH < 1.0f) {
        viewH = itemH;
    }
    const uint32_t colN = cols;
    const uint32_t rows = totalItems == 0 ? 0u : (totalItems + colN - 1u) / colN;
    out.totalHeight = static_cast<float>(rows) * itemH;
    uint32_t windowRows = static_cast<uint32_t>(viewH / itemH + 2.0f);
    if (windowRows < 1u) {
        windowRows = 1u;
    }
    uint32_t firstRow = static_cast<uint32_t>(scrollOffset / itemH);
    if (rows > windowRows && firstRow + windowRows > rows) {
        firstRow = rows - windowRows;
    }
    if (rows <= windowRows || firstRow > rows) {
        firstRow = 0;
    }
    out.first = firstRow * colN;
    uint32_t span = windowRows * colN;
    if (span > kUiPoolCap) {
        span = kUiPoolCap;
    }
    const uint32_t remain = totalItems > out.first ? totalItems - out.first : 0u;
    out.visibleCount = remain < span ? remain : span;
    return out;
}

inline uint16_t uiVirtualSlots(float viewH, float itemH, uint8_t cols = 1) {
    if (cols < 1) {
        cols = 1;
    }
    if (itemH < 1.0f) {
        itemH = 1.0f;
    }
    if (viewH < 1.0f) {
        viewH = itemH;
    }
    uint32_t n = static_cast<uint32_t>((viewH / itemH + 2.0f) * static_cast<float>(cols));
    if (n < 1u) {
        n = 1u;
    }
    if (n > kUiPoolCap) {
        n = kUiPoolCap;
    }
    return static_cast<uint16_t>(n);
}

template <typename T>
T* uiAncestor(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return nullptr;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (auto* comp = s.ent[p].try_get_mut<T>()) {
            return comp;
        }
    }
    return nullptr;
}

template <typename T>
const T* uiAncestor(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return nullptr;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (const T* comp = s.ent[p].try_get<T>()) {
            return comp;
        }
    }
    return nullptr;
}

template <typename T>
uint16_t uiAncestorId(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return kUiNone;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.ent[p].try_get<T>() != nullptr) {
            return p;
        }
    }
    return kUiNone;
}

inline bool uiWithin(const UiState& s, uint16_t id, uint16_t ancestor) {
    if (ancestor == kUiNone || id >= s.count) {
        return false;
    }
    for (uint16_t p = id; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (p == ancestor) {
            return true;
        }
    }
    return false;
}

inline uint32_t uiNodeChildCount(const UiState& s, uint16_t parent) {
    if (parent >= s.count || s.yoga[parent] == nullptr) {
        return 0;
    }
    return YGNodeGetChildCount(s.yoga[parent]);
}

inline uint16_t uiNodeChild(const UiState& s, uint16_t parent, uint32_t index) {
    if (parent >= s.count || s.yoga[parent] == nullptr || index >= YGNodeGetChildCount(s.yoga[parent])) {
        return kUiNone;
    }
    YGNodeRef node = YGNodeGetChild(s.yoga[parent], index);
    for (uint16_t id = 0; id < s.count; ++id) {
        if (s.yoga[id] == node) {
            return id;
        }
    }
    return kUiNone;
}

inline void uiNodeMoveChild(UiState& s, uint16_t parent, uint16_t child, uint32_t newIndex) {
    if (parent >= s.count || child >= s.count) {
        return;
    }
    YGNodeRef host = s.yoga[parent];
    YGNodeRef node = s.yoga[child];
    if (host == nullptr || node == nullptr) {
        return;
    }
    YGNodeRef from = YGNodeGetParent(node);
    if (from == host) {
        const uint32_t count = YGNodeGetChildCount(host);
        uint32_t old = count;
        for (uint32_t i = 0; i < count; ++i) {
            if (YGNodeGetChild(host, i) == node) {
                old = i;
                break;
            }
        }
        if (old == count) {
            return;
        }
        if (newIndex > count) {
            newIndex = count;
        }
        if (old == newIndex) {
            return;
        }
        YGNodeRemoveChild(host, node);
        if (old < newIndex) {
            --newIndex;
        }
    } else if (from != nullptr) {
        YGNodeRemoveChild(from, node);
    }
    const uint32_t live = YGNodeGetChildCount(host);
    if (newIndex > live) {
        newIndex = live;
    }
    YGNodeInsertChild(host, node, newIndex);
    s.layoutDirty = true;
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
void dragSpin(UiState& s, uint16_t id, float x);
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
void fieldRemember(UiState& s, uint16_t id, const UiField& field);
bool fieldUndo(UiState& s);
bool fieldRedo(UiState& s);
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

#pragma once

#include "Str.hpp"
#include "ui/State.hpp"

#include <cstddef>

namespace burnhope {

struct UiScrollMetrics {
    float contentH = 0;
    float maxOff = 0;
    float thumbH = 0;
    float travel = 0;
    float t = 0;
    uint8_t shown = 0;
};

UiScrollMetrics uiCalcScrollMetrics(float viewH, float contentH, float offset);

inline constexpr float kAccent[3][3] = {
    {0.36f, 0.55f, 0.92f},
    {0.45f, 0.72f, 0.48f},
    {0.86f, 0.55f, 0.32f},
};

template <size_t N>
inline uint8_t uiStrCopy(char (&dst)[N], const char* src) {
    return static_cast<uint8_t>(strCopyCap(dst, N, src));
}

inline void copyText(char* dst, uint8_t& len, const char* src) {
    char scratch[32]{};
    len = uiStrCopy(scratch, src);
    for (uint8_t i = 0; i <= len; ++i) {
        dst[i] = scratch[i];
    }
}

inline void uiWriteInt(char* dst, uint8_t& len, int v) {
    if (v < 0) {
        v = 0;
    }
    if (v > 999999) {
        v = 999999;
    }
    char rev[8]{};
    int n = 0;
    int x = v;
    if (x == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        len = 1;
        return;
    }
    while (x > 0 && n < 8) {
        rev[n++] = static_cast<char>('0' + (x % 10));
        x /= 10;
    }
    for (int i = 0; i < n; ++i) {
        dst[i] = rev[n - 1 - i];
    }
    dst[n] = '\0';
    len = static_cast<uint8_t>(n);
}

inline int uiHexNib(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
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

inline void uiNoteChrome(UiState& s, uint16_t id) {
    if (id >= s.count) {
        return;
    }
    const UiPaint* paint = s.ent[id].try_get<UiPaint>();
    if (paint == nullptr) {
        return;
    }
    const int16_t tag = paint->tag;
    if (tag == -5) {
        s.ent[id].set<UiSplitter>({});
        if (auto* live = s.ent[id].try_get_mut<UiPaint>()) {
            const float radius = live->radius;
            *live = paintToken(s.look, UiRole::Panel, UiToken::Muted, radius, tag);
        }
    }
    if (tag == -6) {
        s.ent[id].set<UiTitleBar>({});
    }
    if (tag == -7) {
        s.ent[id].set<UiMenuBar>({});
    }
    if (tag == -15) {
        s.ent[id].set<UiResizable>({});
    }
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
bool uiAncestorHas(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return false;
    }
    for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
        if (s.ent[p].has<T>()) {
            return true;
        }
    }
    return false;
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

uint32_t uiNodeChildCount(const UiState& s, uint16_t parent);
uint16_t uiNodeChild(const UiState& s, uint16_t parent, uint32_t index);
void uiNodeMoveChild(UiState& s, uint16_t parent, uint16_t child, uint32_t newIndex);

inline bool hitBox(const UiBox& b, float x, float y) {
    return b.w > 0.5f && b.h > 0.5f && x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h;
}

inline bool focusable(uint8_t role) {
    return role == static_cast<uint8_t>(UiRole::Button) || role == static_cast<uint8_t>(UiRole::Slider)
        || role == static_cast<uint8_t>(UiRole::Check) || role == static_cast<uint8_t>(UiRole::Field)
        || role == static_cast<uint8_t>(UiRole::Close);
}

uint16_t spawn(UiState& s, uint16_t parent, const UiFlex& flex, const UiPaint& paint);

inline uint16_t uiVirtualHostCreate(UiState& s, uint16_t parent, UiRole role, UiToken token, float radius) {
    UiFlex host{};
    host.grow = 1.0f;
    host.shrink = 1.0f;
    host.overflow = 2;
    host.position = 2;
    const uint16_t scroll = spawn(s, parent, host, paintToken(s.look, role, token, radius));
    if (scroll == kUiNone) {
        return kUiNone;
    }
    UiFlex spacer{};
    spacer.widthMode = static_cast<uint8_t>(UiSize::Percent);
    spacer.width = 100.0f;
    spacer.heightMode = static_cast<uint8_t>(UiSize::Px);
    spacer.height = 1.0f;
    spacer.shrink = 0.0f;
    spacer.position = 2;
    const uint16_t inner = spawn(s, scroll, spacer, paint(UiRole::Hidden, 0, 0, 0));
    s.ent[scroll].set<UiScroll>({0.0f, inner});
    return scroll;
}

inline void uiVirtualSyncSpacer(UiState& s, uint16_t inner, float totalHeight) {
    if (inner >= s.count) {
        return;
    }
    if (auto* flex = s.ent[inner].try_get_mut<UiFlex>()) {
        flex->height = totalHeight;
    }
}

void uiVirtualSetShown(UiState& s, uint16_t slot, bool on);

template <typename Pool, typename Add>
void uiVirtualEnsure(UiState& s, Pool& pool, uint16_t need, uint16_t budget, Add add) {
    while (pool.slots < need && pool.slots < kUiPoolCap && s.count + budget < kUiCap) {
        const uint16_t slot = add();
        if (slot == kUiNone) {
            break;
        }
        pool.slot[pool.slots++] = slot;
    }
}
void addText(UiState& s, uint16_t id, const char* text);
void stamp(UiState& s, uint16_t id, UiToken token);
uint16_t hitTest(const UiState& s, float x, float y);
void activate(UiState& s, uint16_t id);
bool nodeShown(const UiState& s, uint16_t id);
void dragSlider(UiState& s, uint16_t id, float x);
void dragSpin(UiState& s, uint16_t id, float x);
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
void cmdFieldCut(void* user);
void cmdFieldCopy(void* user);
void cmdFieldPaste(void* user);
void cmdFieldAll(void* user);
void cmdFieldWipe(void* user);

} // namespace burnhope

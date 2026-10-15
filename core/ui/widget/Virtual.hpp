#pragma once

#include "ui/State.hpp"
#include "ui/widget/Scroll.hpp"

namespace burnhope {

inline constexpr uint16_t kUiPoolCap = 32;

struct UiVirtualPool {
    void (*refresh)(UiState& s, uint16_t id) = nullptr;
    float (*contentHeight)(const UiState& s, uint16_t id) = nullptr;
    uint8_t follow = 1;
};

struct UiVirtualBase {
    uint32_t count = 0;
    uint32_t first = 0;
    uint16_t slot[kUiPoolCap]{};
    uint8_t slots = 0;
    float itemH = 28.0f;
};

float uiVirtualTotalHeight(const UiState& s, uint16_t id, float itemH, uint32_t count, uint8_t cols = 1);

struct UiVirtual : UiVirtualBase {
    void (*fill)(void* user, uint32_t index, char* dst, uint8_t cap) = nullptr;
    void (*click)(void* user, uint32_t index) = nullptr;
    void* user = nullptr;
};

struct UiVirtRow {
    uint32_t index = 0;
};

template <typename T>
concept UiVirtualHasCols = requires(const T& pool) { pool.cols; };

template <typename T>
float uiVirtualContentHeight(const UiState& s, uint16_t id) {
    const T* pool = id < s.count ? s.ent[id].try_get<T>() : nullptr;
    if (pool == nullptr) {
        return 0.0f;
    }
    uint8_t cols = 1;
    if constexpr (UiVirtualHasCols<T>) {
        cols = pool->cols < 1 ? 1 : pool->cols;
    }
    return uiVirtualTotalHeight(s, id, pool->itemH, pool->count, cols);
}

inline uint32_t uiVirtRowIndex(const UiState& s, uint16_t id) {
    if (id >= s.count) {
        return 0xFFFFFFFFu;
    }
    const UiVirtRow* row = s.ent[id].try_get<UiVirtRow>();
    if (row == nullptr) {
        for (uint16_t p = s.parentOf[id]; p != kUiNone && p < s.count; p = s.parentOf[p]) {
            row = s.ent[p].try_get<UiVirtRow>();
            if (row != nullptr) {
                break;
            }
        }
    }
    return row != nullptr ? row->index : 0xFFFFFFFFu;
}

uint16_t uiVirtual(UiState& s, uint16_t parent, float rowH);
void uiVirtualSource(UiState& s, uint16_t id, uint32_t count, void (*fill)(void*, uint32_t, char*, uint8_t), void (*click)(void*, uint32_t), void* user);
void uiVirtualRefresh(UiState& s, uint16_t id);

float uiVirtualContentH(const UiState& s, uint16_t id);
float uiTreeContentH(const UiState& s, uint16_t id);
float uiGridContentH(const UiState& s, uint16_t id);

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

inline uint16_t uiVirtualHostCreate(UiState& s, uint16_t parent, UiToken token, float radius) {
    UiFlex host{};
    host.grow = 1.0f;
    host.shrink = 1.0f;
    host.overflow = UiOverflow::Scroll;
    host.position = UiPosition::Relative;
    const uint16_t scroll = spawn(s, parent, host, paintToken(s.look, token, radius));
    if (scroll == kUiNone) {
        return kUiNone;
    }
    UiFlex spacer{};
    spacer.widthMode = UiSize::Percent;
    spacer.width = 100.0f;
    spacer.heightMode = UiSize::Px;
    spacer.height = 1.0f;
    spacer.shrink = 0.0f;
    spacer.position = UiPosition::Relative;
    const uint16_t inner = spawn(s, scroll, spacer, paintClear());
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

} // namespace burnhope

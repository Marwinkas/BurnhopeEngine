#pragma once

#include "ui/State.hpp"

namespace burnhope {

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

} // namespace burnhope

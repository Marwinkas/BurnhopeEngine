#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kUndoCap = 32;
constexpr uint32_t kUndoPool = 2048;
constexpr uint8_t kUndoInsert = 1;
constexpr uint8_t kUndoDelete = 2;

struct UndoRecord {
    uint32_t pos = 0;
    uint32_t length = 0;
    uint32_t pool = 0;
    uint8_t kind = 0;
    uint8_t reserved[3]{};
};

struct UndoStack {
    UndoRecord records[kUndoCap]{};
    char pool[kUndoPool]{};
    uint32_t poolUsed = 0;
    uint32_t count = 0;
    uint32_t at = 0;
};

static_assert(std::is_trivially_copyable_v<UndoRecord>);
static_assert(sizeof(UndoRecord) % 16 == 0);
static_assert(std::is_trivially_copyable_v<UndoStack>);

} // namespace burnhope

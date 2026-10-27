#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

// Check widget record. shape 0 is a box with a check icon, shape 1 is a radio.
// The check widget writes the bytes and emits them from its draw hook.
struct UiMark {
    uint8_t shape = 0;
    uint8_t on = 0;
};

static_assert(std::is_trivially_copyable_v<UiMark>);

} // namespace burnhope

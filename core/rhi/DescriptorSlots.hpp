#pragma once

#include <cstdint>

namespace burnhope {

inline constexpr uint32_t kFramesInFlight = 2;

// Слот 48 буфера кучи. Диапазон холста 48..63. Проходы сцены его не занимают.
constexpr uint32_t kUiDescPrims = 48;
static_assert(kUiDescPrims >= 48u && kUiDescPrims < 64u);

} // namespace burnhope

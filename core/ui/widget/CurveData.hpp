#pragma once

#include <type_traits>

namespace burnhope {

struct UiCurve {
    float x[4]{0.0f, 0.25f, 0.75f, 1.0f};
    float y[4]{0.0f, 0.25f, 0.75f, 1.0f};
};

static_assert(std::is_trivially_copyable_v<UiCurve>);

} // namespace burnhope

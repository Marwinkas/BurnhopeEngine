#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

struct EngineTime {
    float rawDelta = 0.0f;
    float fixedDelta = 1.0f / 60.0f;
    float accumulator = 0.0f;
    float alpha = 0.0f;
    uint64_t fixedTickCount = 0;
    float maxAccumulation = 0.1f;
    float reserved = 0.0f;
};

static_assert(std::is_trivially_copyable_v<EngineTime>);
static_assert(sizeof(EngineTime) % 16 == 0);

template <typename FixedTickFn>
void timeAdvance(EngineTime& time, float frameDt, FixedTickFn&& onFixedTick) {
    if (frameDt < 0.0f) {
        frameDt = 0.0f;
    }
    if (time.maxAccumulation > 0.0f && frameDt > time.maxAccumulation) {
        frameDt = time.maxAccumulation;
    }
    time.rawDelta = frameDt;
    time.accumulator += frameDt;
    if (time.fixedDelta <= 1.0e-8f) {
        time.alpha = 0.0f;
        return;
    }
    while (time.accumulator >= time.fixedDelta) {
        onFixedTick(time.fixedDelta);
        time.accumulator -= time.fixedDelta;
        time.fixedTickCount += 1;
    }
    float alpha = time.accumulator / time.fixedDelta;
    if (alpha < 0.0f) {
        alpha = 0.0f;
    }
    if (alpha > 1.0f) {
        alpha = 1.0f;
    }
    time.alpha = alpha;
}

} // namespace burnhope

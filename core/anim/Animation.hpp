#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace burnhope {

// Keyframe tracks. UiAnim stays the one-shot color blend. This samples a slice of keys.
enum class AnimEase : uint8_t { Linear = 0, Step = 1, CubicBezier = 2 };
enum class AnimWrap : uint8_t { Once = 0, Loop = 1, PingPong = 2 };

struct AnimKey {
    float time = 0;
    float value = 0;
};

struct AnimTrack {
    const AnimKey* keys = nullptr;
    uint32_t count = 0;
    AnimEase ease = AnimEase::Linear;
    AnimWrap wrap = AnimWrap::Once;
    float duration = 0;
    float x1 = 0.42f;
    float y1 = 0.0f;
    float x2 = 0.58f;
    float y2 = 1.0f;
};

struct AnimPlayer {
    float currentTime = 0;
    float speed = 1;
    float duration = 0;
    AnimWrap wrap = AnimWrap::Once;
    bool playing = false;
};

static_assert(std::is_trivially_copyable_v<AnimKey>);
static_assert(std::is_trivially_copyable_v<AnimTrack>);
static_assert(std::is_trivially_copyable_v<AnimPlayer>);

inline float animDuration(const AnimTrack& track) {
    if (track.duration > 0.0f) {
        return track.duration;
    }
    if (track.count == 0 || track.keys == nullptr) {
        return 0.0f;
    }
    return track.keys[track.count - 1].time;
}

inline float animWrapTime(float time, float duration, AnimWrap wrap) {
    if (duration <= 0.0f) {
        return 0.0f;
    }
    if (wrap == AnimWrap::Once) {
        return std::clamp(time, 0.0f, duration);
    }
    if (wrap == AnimWrap::Loop) {
        float m = time - duration * static_cast<float>(static_cast<int>(time / duration));
        if (time < 0.0f) {
            m = duration - animWrapTime(-time, duration, AnimWrap::Loop);
            if (m == duration) {
                m = 0.0f;
            }
        }
        if (m < 0.0f) {
            m += duration;
        }
        if (m >= duration) {
            m -= duration;
        }
        return m;
    }
    const float period = duration * 2.0f;
    float m = time - period * static_cast<float>(static_cast<int>(time / period));
    if (m < 0.0f) {
        m += period;
    }
    if (m >= period) {
        m -= period;
    }
    return m <= duration ? m : period - m;
}

inline float animBezier(float u, float c1, float c2) {
    const float o = 1.0f - u;
    return 3.0f * o * o * u * c1 + 3.0f * o * u * u * c2 + u * u * u;
}

inline float animEase(const AnimTrack& track, float u) {
    u = std::clamp(u, 0.0f, 1.0f);
    if (track.ease == AnimEase::Step) {
        return 0.0f;
    }
    if (track.ease != AnimEase::CubicBezier) {
        return u;
    }
    float lo = 0.0f;
    float hi = 1.0f;
    float t = u;
    for (int i = 0; i < 16; ++i) {
        t = (lo + hi) * 0.5f;
        if (animBezier(t, track.x1, track.x2) < u) {
            lo = t;
        } else {
            hi = t;
        }
    }
    return animBezier(t, track.y1, track.y2);
}

inline float animSampleTrack(const AnimTrack& track, float time) {
    if (track.keys == nullptr || track.count == 0) {
        return 0.0f;
    }
    const float duration = animDuration(track);
    const float local = animWrapTime(time, duration, track.wrap);
    if (track.count == 1 || local <= track.keys[0].time) {
        return track.keys[0].value;
    }
    if (local >= track.keys[track.count - 1].time) {
        return track.keys[track.count - 1].value;
    }
    uint32_t lo = 0;
    uint32_t hi = track.count - 1;
    while (hi - lo > 1) {
        const uint32_t mid = lo + (hi - lo) / 2;
        if (track.keys[mid].time <= local) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    const AnimKey& a = track.keys[lo];
    const AnimKey& b = track.keys[hi];
    const float span = b.time - a.time;
    if (span <= 0.0f || track.ease == AnimEase::Step) {
        return a.value;
    }
    const float u = animEase(track, (local - a.time) / span);
    return a.value + (b.value - a.value) * u;
}

inline void animTickPlayers(AnimPlayer* players, uint32_t count, float dt) {
    if (players == nullptr) {
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        AnimPlayer& p = players[i];
        if (!p.playing) {
            continue;
        }
        p.currentTime = animWrapTime(p.currentTime + dt * p.speed, p.duration, p.wrap);
        if (p.wrap == AnimWrap::Once && p.duration > 0.0f && p.currentTime >= p.duration) {
            p.playing = false;
        }
    }
}

} // namespace burnhope

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace burnhope {

// Init/load: TLSF/mimalloc later. Frame: bump, reset every frame. No new in loop.
struct FrameArena {
    std::byte* mem = nullptr;
    std::size_t cap = 0;
    std::size_t off = 0;
};

[[nodiscard]] bool frameArenaCreate(FrameArena& a, std::size_t bytes);
void frameArenaDestroy(FrameArena& a);
void frameArenaReset(FrameArena& a);

[[nodiscard]] void* frameArenaAlloc(FrameArena& a, std::size_t bytes, std::size_t align = 16);

template <class T>
[[nodiscard]] T* frameArenaNew(FrameArena& a) {
    static_assert(alignof(T) <= 64);
    return static_cast<T*>(frameArenaAlloc(a, sizeof(T), alignof(T)));
}

} // namespace burnhope

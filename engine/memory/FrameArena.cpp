#include "memory/FrameArena.hpp"

#include <cstdlib>

namespace burnhope {

bool frameArenaCreate(FrameArena& a, std::size_t bytes) {
    frameArenaDestroy(a);
    if (bytes == 0) {
        return false;
    }
    a.mem = static_cast<std::byte*>(std::aligned_alloc(64, bytes));
    if (a.mem == nullptr) {
        return false;
    }
    a.cap = bytes;
    a.off = 0;
    return true;
}

void frameArenaDestroy(FrameArena& a) {
    std::free(a.mem);
    a.mem = nullptr;
    a.cap = 0;
    a.off = 0;
}

void frameArenaReset(FrameArena& a) {
    a.off = 0;
}

void* frameArenaAlloc(FrameArena& a, std::size_t bytes, std::size_t align) {
    if (a.mem == nullptr || bytes == 0) {
        return nullptr;
    }
    const std::size_t mask = align - 1;
    const std::size_t aligned = (a.off + mask) & ~mask;
    if (aligned + bytes > a.cap) {
        return nullptr;
    }
    a.off = aligned + bytes;
    return a.mem + aligned;
}

} // namespace burnhope

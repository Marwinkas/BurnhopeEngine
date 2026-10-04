#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

struct StreamStagingRing {
    uint8_t* basePtr = nullptr;
    uint32_t capacity = 0;
    uint32_t chunkSize = 65536;
    uint32_t head = 0;
    uint32_t tail = 0;
    uint32_t freeChunks = 0;
};

static_assert(std::is_trivially_copyable_v<StreamStagingRing>);

inline void streamRingInit(StreamStagingRing& ring, uint8_t* base, uint32_t capacity, uint32_t chunkSize) {
    ring = {};
    ring.basePtr = base;
    ring.chunkSize = chunkSize == 0 ? 1u : chunkSize;
    ring.capacity = (capacity / ring.chunkSize) * ring.chunkSize;
    ring.freeChunks = ring.chunkSize == 0 ? 0 : ring.capacity / ring.chunkSize;
}

inline uint8_t* streamRingAlloc(StreamStagingRing& ring, uint32_t size, uint32_t* outOffset) {
    if (ring.basePtr == nullptr || ring.chunkSize == 0 || size == 0 || outOffset == nullptr) {
        return nullptr;
    }
    const uint32_t chunks = (size + ring.chunkSize - 1u) / ring.chunkSize;
    const uint32_t bytes = chunks * ring.chunkSize;
    if (chunks > ring.freeChunks || bytes > ring.capacity) {
        return nullptr;
    }
    if (ring.head + bytes > ring.capacity) {
        if (bytes > ring.tail) {
            return nullptr;
        }
        ring.head = 0;
    }
    if (ring.head < ring.tail && ring.head + bytes > ring.tail) {
        return nullptr;
    }
    *outOffset = ring.head;
    uint8_t* ptr = ring.basePtr + ring.head;
    ring.head += bytes;
    if (ring.head == ring.capacity) {
        ring.head = 0;
    }
    ring.freeChunks -= chunks;
    return ptr;
}

inline void streamRingRelease(StreamStagingRing& ring, uint32_t offset, uint32_t size) {
    if (ring.chunkSize == 0 || size == 0) {
        return;
    }
    const uint32_t chunks = (size + ring.chunkSize - 1u) / ring.chunkSize;
    const uint32_t bytes = chunks * ring.chunkSize;
    if (offset != ring.tail) {
        return;
    }
    ring.tail += bytes;
    if (ring.tail >= ring.capacity) {
        ring.tail = 0;
    }
    ring.freeChunks += chunks;
    if (ring.freeChunks * ring.chunkSize > ring.capacity) {
        ring.freeChunks = ring.capacity / ring.chunkSize;
    }
    if (ring.freeChunks == ring.capacity / ring.chunkSize) {
        ring.head = 0;
        ring.tail = 0;
    }
}

} // namespace burnhope

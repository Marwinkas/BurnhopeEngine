#include "MurmurHash3.hpp"
#include <atomic>
#include <chrono>

namespace burnhope::hash {

    uint64_t GenerateGUID() noexcept {
        static std::atomic<uint64_t> counter{1};
        uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
        uint64_t timeBits = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());

        uint64_t mixed[2] = { seq, timeBits };
        uint64_t id = Murmur3_64(mixed, sizeof(mixed));
        return id == 0 ? 1 : id; // 0 reserved as "null" GUID
    }
}

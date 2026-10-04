#pragma once

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kStreamQueueCap = 128;

constexpr uint8_t kStreamPending = 0;
constexpr uint8_t kStreamInFlight = 1;
constexpr uint8_t kStreamComplete = 2;
constexpr uint8_t kStreamFailed = 3;

struct StreamRequest {
    uint64_t fileOffset = 0;
    uint64_t gpuTargetAddress = 0;
    uint32_t assetHash = 0;
    uint32_t byteSize = 0;
    float priority = 0.0f;
    uint8_t status = kStreamPending;
    uint8_t pad[3]{};
};

static_assert(std::is_trivially_copyable_v<StreamRequest>);
static_assert(sizeof(StreamRequest) == 32);

struct StreamQueue {
    StreamRequest requests[kStreamQueueCap]{};
    uint32_t count = 0;
};

inline bool streamQueuePush(StreamQueue& queue, const StreamRequest& request) {
    if (queue.count >= kStreamQueueCap) {
        return false;
    }
    queue.requests[queue.count] = request;
    queue.count += 1;
    return true;
}

inline int32_t streamQueuePick(const StreamQueue& queue) {
    int32_t best = -1;
    float bestPriority = 0.0f;
    for (uint32_t i = 0; i < queue.count; ++i) {
        if (queue.requests[i].status != kStreamPending) {
            continue;
        }
        if (best < 0 || queue.requests[i].priority < bestPriority) {
            best = static_cast<int32_t>(i);
            bestPriority = queue.requests[i].priority;
        }
    }
    return best;
}

} // namespace burnhope

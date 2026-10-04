#pragma once

#include "stream/StreamQueue.hpp"

#include <cstdint>
#include <type_traits>

namespace burnhope {

constexpr uint32_t kResidencyCap = 256;

constexpr uint8_t kResidencyLoading = 0;
constexpr uint8_t kResidencyResident = 1;

struct ResidencySlot {
    uint32_t assetHash = 0;
    uint8_t state = kResidencyLoading;
    uint8_t occupied = 0;
    uint8_t pad[2]{};
    uint32_t reserved = 0;
};

static_assert(std::is_trivially_copyable_v<ResidencySlot>);
static_assert(sizeof(ResidencySlot) == 12);

struct ResidencyTable {
    ResidencySlot slots[kResidencyCap]{};
};

inline ResidencySlot* residencyFind(ResidencyTable& table, uint32_t assetHash, bool create) {
    const uint32_t start = assetHash % kResidencyCap;
    for (uint32_t n = 0; n < kResidencyCap; ++n) {
        ResidencySlot& slot = table.slots[(start + n) % kResidencyCap];
        if (slot.occupied != 0 && slot.assetHash == assetHash) {
            return &slot;
        }
        if (slot.occupied == 0) {
            if (!create) {
                return nullptr;
            }
            slot.occupied = 1;
            slot.assetHash = assetHash;
            slot.state = kResidencyLoading;
            return &slot;
        }
    }
    return nullptr;
}

inline bool streamCommitResident(StreamRequest& request, ResidencyTable& table) {
    ResidencySlot* slot = residencyFind(table, request.assetHash, true);
    if (slot == nullptr) {
        request.status = kStreamFailed;
        return false;
    }
    request.status = kStreamComplete;
    slot->state = kResidencyResident;
    return true;
}

} // namespace burnhope

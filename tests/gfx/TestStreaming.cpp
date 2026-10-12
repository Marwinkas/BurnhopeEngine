#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "stream/StreamRing.hpp"
#include "stream/StreamQueue.hpp"
#include "stream/ResidencyTable.hpp"
#include "io/File.hpp"
#include "io/Blob.hpp"

#include <cstdio>
#include <cstring>

using namespace burnhope;

TEST_CASE("stream ring wraps after the first chunks are released") {
    alignas(16) static uint8_t storage[4 * 65536];
    StreamStagingRing ring{};
    streamRingInit(ring, storage, sizeof(storage), 65536);
    uint32_t off[4]{};
    uint8_t* ptr[4]{};
    for (uint32_t i = 0; i < 4; ++i) {
        ptr[i] = streamRingAlloc(ring, 65536, &off[i]);
        CHECK(ptr[i] != nullptr);
        CHECK(off[i] == i * 65536);
    }
    CHECK(ring.freeChunks == 0);
    CHECK(streamRingAlloc(ring, 65536, &off[0]) == nullptr);
    streamRingRelease(ring, 0, 65536);
    streamRingRelease(ring, 65536, 65536);
    CHECK(ring.freeChunks == 2);
    uint32_t wrapped = 99;
    uint8_t* again = streamRingAlloc(ring, 65536, &wrapped);
    CHECK(again == storage);
    CHECK(wrapped == 0);
}
TEST_CASE("stream queue picks the nearest asset") {
    StreamQueue queue{};
    StreamRequest far{};
    far.assetHash = 3;
    far.priority = 12.0f;
    StreamRequest mid{};
    mid.assetHash = 2;
    mid.priority = 4.0f;
    StreamRequest nearAsset{};
    nearAsset.assetHash = 1;
    nearAsset.priority = 0.5f;
    CHECK(streamQueuePush(queue, far));
    CHECK(streamQueuePush(queue, mid));
    CHECK(streamQueuePush(queue, nearAsset));
    const int32_t pick = streamQueuePick(queue);
    CHECK(pick == 2);
    CHECK(queue.requests[pick].assetHash == 1);
}
TEST_CASE("stream commit marks the mapped blob resident") {
    const char* path = "/tmp/burnhope-stream.bin";
    const uint8_t body[4] = {1, 2, 3, 4};
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(body, 1, 4, file) == 4);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    alignas(16) uint8_t staging[65536];
    StreamStagingRing ring{};
    streamRingInit(ring, staging, sizeof(staging), 65536);
    uint32_t offset = 0;
    uint8_t* dst = streamRingAlloc(ring, static_cast<uint32_t>(mapped.size), &offset);
    CHECK(dst != nullptr);
    std::memcpy(dst, mapped.data, mapped.size);
    CHECK(std::memcmp(dst, body, 4) == 0);
    StreamRequest request{};
    request.assetHash = assetHash("stream-blob");
    request.byteSize = static_cast<uint32_t>(mapped.size);
    request.gpuTargetAddress = 0x1000;
    request.status = kStreamInFlight;
    ResidencyTable table{};
    CHECK(streamCommitResident(request, table));
    CHECK(request.status == kStreamComplete);
    ResidencySlot* slot = residencyFind(table, request.assetHash, false);
    CHECK(slot != nullptr);
    CHECK(slot->state == kResidencyResident);
    fileUnmap(&mapped);
    streamRingRelease(ring, offset, request.byteSize);
}

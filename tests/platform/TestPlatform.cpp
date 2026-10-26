#define DOCTEST_CONFIG_NO_EXCEPTIONS
#include <doctest/doctest.h>

#include "host/Host.hpp"
#include "memory/FrameArena.hpp"
#include "io/File.hpp"
#include "platform/Jobs.hpp"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <spdlog/spdlog.h>

using namespace burnhope;


struct JobProbe {
    std::mutex* gate = nullptr;
    int* shared = nullptr;
    std::atomic<int>* done = nullptr;
};

void jobProbe(void* user) {
    auto* probe = static_cast<JobProbe*>(user);
    for (int i = 0; i < 4000; ++i) {
        std::lock_guard<std::mutex> lock(*probe->gate);
        *probe->shared += 1;
    }
    probe->done->fetch_add(1, std::memory_order_release);
}

TEST_CASE("tsan jobs run without lost updates") {
    std::mutex gate;
    int shared = 0;
    std::atomic<int> done{0};
    JobProbe probes[2]{{&gate, &shared, &done}, {&gate, &shared, &done}};
    Job jobs[2]{
        {&jobProbe, &probes[0]},
        {&jobProbe, &probes[1]},
    };
    JobPool* pool = jobsCreate();
    jobsRunAndWait(*pool, jobs, 2);
    std::fprintf(stderr, "tsan jobs shared=%d done=%d workers=%d\n", shared, done.load(), jobsWorkerCount(*pool));
    jobsDestroy(pool);
    CHECK(shared == 8000);
    CHECK(done.load() == 2);
}

TEST_CASE("tsan host ticks two hidden windows") {
    Host host{};
    if (!hostInit(host)) {
        return;
    }
    ViewDesc a{};
    a.name = "tsan-a";
    a.window.hidden = true;
    a.window.width = 320;
    a.window.height = 180;
    ViewDesc b = a;
    b.name = "tsan-b";
    const int sa = hostOpen(host, a);
    const int sb = hostOpen(host, b);
    std::fprintf(stderr, "tsan open %d %d\n", sa, sb);
    if (sa < 0 || sb < 0) {
        spdlog::warn("tsan host: no display or second window, skip");
        hostShutdown(host);
        return;
    }
    spdlog::info("tsan host: two windows, {} workers", jobsWorkerCount(deviceJobs(host.device)));
    bool ok = true;
    for (int i = 0; i < 24; ++i) {
        if (!hostTick(host)) {
            ok = false;
            break;
        }
    }
    hostShutdown(host);
    spdlog::info("tsan host: {}", ok ? "frames done" : "tick failed");
    CHECK(ok);
}
TEST_CASE("arena reset drops the bump") {
    FrameArena arena;
    CHECK(frameArenaCreate(arena, 4096));
    CHECK(frameArenaAlloc(arena, 128) != nullptr);
    CHECK(arena.off > 0);
    frameArenaReset(arena);
    CHECK(arena.off == 0);
    frameArenaDestroy(arena);
    CHECK(arena.mem == nullptr);
}
TEST_CASE("mapped file reads the bytes that were written") {
    const char* path = "/tmp/burnhope-map-test.bin";
    const char bytes[] = "BHOP-map";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    CHECK(std::fwrite(bytes, 1, 8, file) == 8);
    std::fclose(file);
    MappedFile mapped{};
    CHECK(fileMapReadOnly(path, &mapped));
    CHECK(mapped.size == 8);
    CHECK(mapped.data != nullptr);
    CHECK(std::memcmp(mapped.data, bytes, 8) == 0);
    fileUnmap(&mapped);
    CHECK(mapped.data == nullptr);
    CHECK(mapped.fd < 0);
    std::remove(path);
}

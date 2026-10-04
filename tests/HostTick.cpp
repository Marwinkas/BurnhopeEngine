#include "host/Host.hpp"
#include "platform/Jobs.hpp"

#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace burnhope {
void uiBuildShell(UiState&) {}
}

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

int main() {
    std::mutex gate;
    int shared = 0;
    std::atomic<int> done{0};
    JobProbe probes[2]{{&gate, &shared, &done}, {&gate, &shared, &done}};
    burnhope::Job jobs[2]{
        {&jobProbe, &probes[0]},
        {&jobProbe, &probes[1]},
    };
    burnhope::jobsRunAndWait(jobs, 2);
    std::fprintf(stderr, "tsan jobs shared=%d done=%d workers=%d\n", shared, done.load(), burnhope::jobsWorkerCount());
    if (shared != 8000 || done.load() != 2) {
        return 1;
    }

    burnhope::Host host{};
    if (!burnhope::hostInit(host)) {
        return 1;
    }
    burnhope::ViewDesc a{};
    a.name = "tsan-a";
    a.window.hidden = true;
    a.window.width = 320;
    a.window.height = 180;
    burnhope::ViewDesc b = a;
    b.name = "tsan-b";
    const int sa = burnhope::hostOpen(host, a);
    const int sb = burnhope::hostOpen(host, b);
    std::fprintf(stderr, "tsan open %d %d\n", sa, sb);
    if (sa < 0 || sb < 0) {
        spdlog::warn("tsan host: no display or second window, skip");
        burnhope::hostShutdown(host);
        return 0;
    }
    spdlog::info("tsan host: two windows, {} workers", burnhope::jobsWorkerCount());
    bool ok = true;
    for (int i = 0; i < 24; ++i) {
        if (!burnhope::hostTick(host)) {
            ok = false;
            break;
        }
    }
    burnhope::hostShutdown(host);
    spdlog::info("tsan host: {}", ok ? "frames done" : "tick failed");
    return ok ? 0 : 1;
}

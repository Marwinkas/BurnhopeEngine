#include "platform/Jobs.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace burnhope {
namespace {

struct Pool {
    std::vector<std::thread> workers;
    std::mutex m;
    std::condition_variable cv;
    Job* jobs = nullptr;
    int count = 0;
    std::atomic<int> next{0};
    std::atomic<int> remaining{0};
    uint64_t generation = 0;
    bool stop = false;

    void runSlice(Job* j, int n) {
        for (;;) {
            const int i = next.fetch_add(1, std::memory_order_relaxed);
            if (i >= n) {
                break;
            }
            j[i].fn(j[i].user);
            remaining.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void workerLoop() {
        uint64_t seen = 0;
        for (;;) {
            Job* localJobs = nullptr;
            int localCount = 0;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return stop || generation != seen; });
                if (stop) {
                    return;
                }
                seen = generation;
                localJobs = jobs;
                localCount = count;
            }
            runSlice(localJobs, localCount);
        }
    }

    ~Pool() {
        {
            std::lock_guard<std::mutex> lk(m);
            stop = true;
        }
        cv.notify_all();
        for (auto& t : workers) {
            if (t.joinable()) {
                t.join();
            }
        }
    }
};

Pool& pool() {
    static Pool p;
    static std::once_flag once;
    std::call_once(once, [] {
        const unsigned hw = std::thread::hardware_concurrency();
        // Один поток остаётся вызывающему (он сам тянет работу), остальные в пул.
        // Верхняя граница 7 — больше окон/задач за кадр в этом движке не бывает.
        const unsigned extra = hw > 1 ? std::min(hw - 1u, 7u) : 0u;
        for (unsigned i = 0; i < extra; ++i) {
            p.workers.emplace_back([&p] { p.workerLoop(); });
        }
    });
    return p;
}

} // namespace

int jobsWorkerCount() {
    return static_cast<int>(pool().workers.size()) + 1;
}

void jobsRunAndWait(Job* jobs, int count) {
    if (count <= 0) {
        return;
    }
    if (count == 1) {
        jobs[0].fn(jobs[0].user);
        return;
    }
    Pool& p = pool();
    {
        std::lock_guard<std::mutex> lk(p.m);
        p.jobs = jobs;
        p.count = count;
        p.next.store(0, std::memory_order_relaxed);
        p.remaining.store(count, std::memory_order_relaxed);
        p.generation += 1;
    }
    p.cv.notify_all();
    p.runSlice(jobs, count);
    while (p.remaining.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
}

} // namespace burnhope

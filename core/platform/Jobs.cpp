#include "platform/Jobs.hpp"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace burnhope {
namespace {

struct Batch {
    Job* jobs = nullptr;
    int count = 0;
    std::atomic<int> next{0};
    std::atomic<int> remaining{0};
    std::atomic<int> engaged{0};
};

struct Pool {
    std::vector<std::thread> workers;
    std::mutex m;
    std::condition_variable cv;
    std::condition_variable idle;
    Batch* current = nullptr;
    uint64_t epoch = 0;
    bool stop = false;

    static void runSlice(Batch& batch) {
        for (;;) {
            const int i = batch.next.fetch_add(1, std::memory_order_relaxed);
            if (i >= batch.count) {
                break;
            }
            batch.jobs[i].fn(batch.jobs[i].user);
            batch.remaining.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void workerLoop() {
        uint64_t seen = 0;
        for (;;) {
            Batch* batch = nullptr;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [&] { return stop || epoch != seen; });
                if (stop && current == nullptr) {
                    return;
                }
                seen = epoch;
                batch = current;
                if (batch != nullptr) {
                    batch->engaged.fetch_add(1, std::memory_order_acq_rel);
                }
            }
            if (batch == nullptr) {
                continue;
            }
            runSlice(*batch);
            batch->engaged.fetch_sub(1, std::memory_order_acq_rel);
            idle.notify_all();
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
        // Вызывающий поток тоже берёт работу. Остальные ядра ждут в пуле.
        const unsigned extra = hw > 1 ? hw - 1u : 0u;
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
    Batch batch;
    batch.jobs = jobs;
    batch.count = count;
    batch.next.store(0, std::memory_order_relaxed);
    batch.remaining.store(count, std::memory_order_relaxed);
    {
        std::unique_lock<std::mutex> lk(p.m);
        p.idle.wait(lk, [&] { return p.current == nullptr; });
        p.current = &batch;
        p.epoch += 1;
    }
    p.cv.notify_all();
    Pool::runSlice(batch);
    while (batch.remaining.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
    {
        std::unique_lock<std::mutex> lk(p.m);
        p.idle.wait(lk, [&] { return batch.engaged.load(std::memory_order_acquire) == 0; });
        p.current = nullptr;
    }
    p.idle.notify_all();
}

} // namespace burnhope

// JobSystem — fixed-size worker pool with a blocking ParallelFor. This is the multi-core entry point for the
// shipping (ECS) simulation layer; the faithful src/sc4 classes stay single-threaded reference code.
// Work is split into contiguous index ranges, so each worker streams over its own slice of the packed arrays.
#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace ecs {

class JobSystem {
public:
    // workers == 0 -> one per hardware thread (the caller also works, so workers-1 threads are spawned).
    explicit JobSystem(unsigned workers = 0) {
        if (workers == 0) workers = std::max(1u, std::thread::hardware_concurrency());
        workerCount_ = workers;
        for (unsigned i = 1; i < workers; ++i) threads_.emplace_back([this] { WorkerLoop(); });
    }
    ~JobSystem() {
        { std::lock_guard<std::mutex> l(mutex_); quit_ = true; ++generation_; }
        wake_.notify_all();
        for (auto& t : threads_) t.join();
    }
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    unsigned WorkerCount() const { return workerCount_; }

    // Runs body(begin, end) over [0, count) in chunks of at least minChunk items; returns when all are done.
    // Not re-entrant: call from one thread at a time.
    void ParallelFor(std::size_t count, std::size_t minChunk, const std::function<void(std::size_t, std::size_t)>& body) {
        if (count == 0) return;
        if (minChunk == 0) minChunk = 1;
        std::size_t chunks = std::min<std::size_t>(workerCount_ * 4, (count + minChunk - 1) / minChunk);
        if (chunks <= 1 || workerCount_ == 1) { body(0, count); return; }
        std::size_t chunkSize = (count + chunks - 1) / chunks;
        {
            std::lock_guard<std::mutex> l(mutex_);
            body_ = &body; count_ = count; chunkSize_ = chunkSize;
            next_.store(0, std::memory_order_relaxed);
            pending_.store(chunks, std::memory_order_relaxed);
            ++generation_;
        }
        wake_.notify_all();
        RunChunks();
        std::unique_lock<std::mutex> l(mutex_);
        done_.wait(l, [this] { return pending_.load(std::memory_order_acquire) == 0; });
        body_ = nullptr;
    }

private:
    void RunChunks() {
        for (;;) {
            std::size_t begin = next_.fetch_add(chunkSize_, std::memory_order_relaxed);
            if (begin >= count_) return;
            (*body_)(begin, std::min(begin + chunkSize_, count_));
            if (pending_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> l(mutex_);
                done_.notify_all();
            }
        }
    }
    void WorkerLoop() {
        std::size_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> l(mutex_);
                wake_.wait(l, [&] { return generation_ != seen; });
                seen = generation_;
                if (quit_) return;
            }
            RunChunks();
        }
    }

    unsigned workerCount_ = 1;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_, done_;
    std::size_t generation_ = 0;
    bool quit_ = false;
    const std::function<void(std::size_t, std::size_t)>* body_ = nullptr;
    std::size_t count_ = 0, chunkSize_ = 1;
    std::atomic<std::size_t> next_{0}, pending_{0};
};

}  // namespace ecs
